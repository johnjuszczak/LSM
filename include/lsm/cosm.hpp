#pragma once

#include <atomic>
#include <chrono>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <functional>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <lsm/detail/helpers.hpp>
#include <lsm/detail/machine_impl.hpp>
#include <lsm/detail/policy.hpp>

namespace lsm
{

template <typename State,
          typename Input,
          typename Output = std::monostate,
          typename Context = std::monostate,
          typename CallablePolicy = policy::copy,
          typename EffectPolicy = policy::ReturnOutput<Output>>
using CoMachine = MachineImpl<State, Input, Output, Context, CallablePolicy, EffectPolicy>;

namespace co
{

template <class T = void>
class Task;

template <class T>
class Task
{
    static_assert(!std::is_void_v<T>, "Use Task<void> specialization for void return");

public:
    using value_t = T;

    struct promise_type
    {
        std::exception_ptr eptr;
        std::coroutine_handle<> continuation{};
        std::optional<value_t> value;

        Task get_return_object()
        {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        struct final_awaiter
        {
            bool await_ready() const noexcept
            {
                return false;
            }
            void await_suspend(std::coroutine_handle<promise_type> h) noexcept
            {
                if(h.promise().continuation) h.promise().continuation.resume();
            }
            void await_resume() const noexcept {}
        };
        final_awaiter final_suspend() noexcept
        {
            return {};
        }

        template <class V>
        void return_value(V&& v)
        {
            value.emplace(std::forward<V>(v));
        }

        void unhandled_exception()
        {
            eptr = std::current_exception();
        }
    };

    using handle = std::coroutine_handle<promise_type>;

    Task() = default;
    explicit Task(handle h) : handle_(h) {}
    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    Task& operator=(Task&& other) noexcept
    {
        if(this != &other)
        {
            if(handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    ~Task()
    {
        if(handle_) handle_.destroy();
    }

    bool await_ready() const noexcept
    {
        return !handle_ || handle_.done();
    }
    void await_suspend(std::coroutine_handle<> c) noexcept
    {
        handle_.promise().continuation = c;
        handle_.resume();
    }
    value_t await_resume()
    {
        if(handle_ && handle_.promise().eptr) std::rethrow_exception(handle_.promise().eptr);
        return std::move(*handle_.promise().value);
    }

private:
    handle handle_{};
};

template <>
class Task<void>
{
public:
    using value_t = std::monostate;

    struct promise_type
    {
        std::exception_ptr eptr;
        std::coroutine_handle<> continuation{};

        Task get_return_object()
        {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        struct final_awaiter
        {
            bool await_ready() const noexcept
            {
                return false;
            }
            void await_suspend(std::coroutine_handle<promise_type> h) noexcept
            {
                if(h.promise().continuation) h.promise().continuation.resume();
            }
            void await_resume() const noexcept {}
        };
        final_awaiter final_suspend() noexcept
        {
            return {};
        }

        void return_void() noexcept {}

        void unhandled_exception()
        {
            eptr = std::current_exception();
        }
    };

    using handle = std::coroutine_handle<promise_type>;

    Task() = default;
    explicit Task(handle h) : handle_(h) {}
    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    Task& operator=(Task&& other) noexcept
    {
        if(this != &other)
        {
            if(handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    ~Task()
    {
        if(handle_) handle_.destroy();
    }

    bool await_ready() const noexcept
    {
        return !handle_ || handle_.done();
    }
    void await_suspend(std::coroutine_handle<> c) noexcept
    {
        handle_.promise().continuation = c;
        handle_.resume();
    }
    void await_resume()
    {
        if(handle_ && handle_.promise().eptr) std::rethrow_exception(handle_.promise().eptr);
    }

private:
    handle handle_{};
};

template <class A>
concept Awaitable =
    requires(A a) {
        { a.await_ready() } -> std::convertible_to<bool>;
        a.await_suspend(std::coroutine_handle<>{});
        a.await_resume();
    } || requires(A a) {
        a.operator co_await();
    };

struct CancelToken;

struct CancelSource
{
    std::atomic<bool> stop{false};
    void request_stop() noexcept
    {
        stop.store(true, std::memory_order_relaxed);
    }
    void reset() noexcept
    {
        stop.store(false, std::memory_order_relaxed);
    }
    CancelToken token() noexcept;
};

struct CancelToken
{
    const CancelSource* src{};
    constexpr CancelToken() noexcept = default;
    constexpr explicit CancelToken(const CancelSource* source) noexcept : src(source) {}
    bool stop_requested() const noexcept
    {
        return src && src->stop.load(std::memory_order_relaxed);
    }
};

inline CancelToken CancelSource::token() noexcept
{
    return CancelToken{this};
}

struct cancelled_error : std::exception
{
    const char* what() const noexcept override
    {
        return "lsm::co cancelled";
    }
};

inline void throw_if_cancelled(CancelToken token)
{
    if(token.stop_requested())
    {
        throw cancelled_error{};
    }
}

struct cancellation_awaitable
{
    CancelToken token;
    bool await_ready() const noexcept
    {
        return token.stop_requested();
    }
    bool await_suspend(std::coroutine_handle<>) const noexcept
    {
        return false;
    }
    void await_resume() const
    {
        throw_if_cancelled(token);
    }
};

inline auto cancelled(CancelToken token)
{
    return cancellation_awaitable{token};
}

struct scheduler
{
    struct immediate
    {
        constexpr bool await_ready() const noexcept
        {
            return true;
        }
        constexpr void await_suspend(std::coroutine_handle<>) const noexcept {}
        constexpr void await_resume() const noexcept {}
    };

    immediate post() const noexcept
    {
        return {};
    }
    immediate yield() const noexcept
    {
        return {};
    }

    template <class Rep, class Period>
    immediate sleep_for(std::chrono::duration<Rep, Period>) const noexcept
    {
        return {};
    }
};

template <class CoMachine>
struct types
{
    using State = typename CoMachine::State_t;
    using Input = typename CoMachine::Input_t;
    using Output = typename CoMachine::Output_t;
    using Context = typename CoMachine::Ctx_t;
    using Publisher = typename CoMachine::Publisher_t;

    template <typename Sig>
    using Callable = typename CoMachine::template Callable<Sig>;

    using CoAction = Callable<Task<std::optional<Output>>(const Input&, Context&, CancelToken, Publisher&)>;
};

template <class CoMachine>
class Registry
{
    using traits = types<CoMachine>;
    using State = typename traits::State;
    using CoAction = typename traits::CoAction;

    struct Key
    {
        State from;
        State to;
        bool operator==(const Key& other) const noexcept
        {
            return from == other.from && to == other.to;
        }
    };

    struct KeyHash
    {
        std::size_t operator()(const Key& key) const noexcept
        {
            auto hf = [](const State& s) -> std::size_t {
                if constexpr(std::is_enum_v<State>)
                {
                    using U = std::underlying_type_t<State>;
                    return std::hash<U>{}(static_cast<U>(s));
                }
                else
                {
                    return std::hash<State>{}(s);
                }
            };
            std::size_t h1 = hf(key.from);
            std::size_t h2 = hf(key.to);
            std::size_t seed = h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) + (h1 >> 2));
            return seed;
        }
    };

public:
    void add(State from, State to, CoAction action)
    {
        map_[Key{from, to}].push_back(std::move(action));
    }

    std::vector<CoAction>* find(State from, State to)
    {
        auto it = map_.find(Key{from, to});
        return it == map_.end() ? nullptr : &it->second;
    }

private:
    std::unordered_map<Key, std::vector<CoAction>, KeyHash> map_;
};

template <class CoMachine>
class Adapter
{
public:
    using traits = types<CoMachine>;
    using State = typename traits::State;
    using Input = typename traits::Input;
    using Output = typename traits::Output;
    using Context = typename traits::Context;
    using Publisher = typename traits::Publisher;
    using CoAction = typename traits::CoAction;

    explicit Adapter(CoMachine& machine, CancelSource* global_cancel = nullptr)
        : machine_(machine), global_cancel_(global_cancel) {}

    void bind_async(State from, State to, CoAction action)
    {
        registry_.add(from, to, std::move(action));
    }

    class FromStage;
    template <class Event>
    class OnStage;
    class ToStage;

    FromStage from(State state)
    {
        return FromStage(*this, state);
    }

    Task<std::optional<Output>> dispatch_async(Input in)
    {
        auto sel = machine_.select(in);
        if(sel)
        {
            const State from = machine_.state();
            const State to = sel.get()->to;

            if(auto* actions = registry_.find(from, to); actions && !actions->empty())
            {
                auto completion_out = machine_.commit(sel, &in);
                machine_.begin_async_effect();
                CancelToken token{global_cancel_};
                std::optional<Output> effect_out;
                try
                {
                    effect_out = co_await (*actions)[0](in, machine_.context(), token, machine_.publisher());
                } catch(...)
                {
                    machine_.end_async_effect();
                    throw;
                }
                machine_.end_async_effect();
                if(effect_out)
                {
                    co_return effect_out;
                }
                co_return completion_out;
            }

            co_return machine_.commit(sel, &in);
        }

        co_return std::optional<Output>{};
    }

private:
    CoMachine& machine_;
    Registry<CoMachine> registry_;
    CancelSource* global_cancel_ = nullptr;
};

template <class CoMachine>
class Adapter<CoMachine>::FromStage
{
public:
    FromStage(Adapter& adapter, State state)
        : adapter_(adapter), from_(state) {}

    template <class Event>
    OnStage<Event> on()
    {
        return OnStage<Event>(adapter_, from_);
    }

private:
    Adapter& adapter_;
    State from_;
};

template <class CoMachine>
template <class Event>
class Adapter<CoMachine>::OnStage
{
public:
    OnStage(Adapter& adapter, State from)
        : adapter_(adapter), from_(from) {}

    ToStage to(State to)
    {
        return ToStage(adapter_, from_, to);
    }

private:
    Adapter& adapter_;
    State from_;
};

template <class CoMachine>
class Adapter<CoMachine>::ToStage
{
public:
    using Fragment = typename traits::template Callable<Task<std::optional<Output>>(const Input&, Context&, CancelToken, Publisher&)>;

    ToStage(Adapter& adapter, State from, State to)
        : adapter_(adapter), from_(from), to_(to) {}

    template <class Fn>
    ToStage& await(Fn fn)
    {
        fragments_.push_back([fn = std::move(fn)](const Input& in, Context& ctx, CancelToken tok, Publisher& publisher) -> Task<std::optional<Output>> {
            co_await fn(in, ctx, tok, publisher);
            co_return std::nullopt;
        });
        return *this;
    }

    template <class Fn>
    ToStage& emit(Fn fn)
    {
        fragments_.push_back([fn = std::move(fn)](const Input& in, Context& ctx, CancelToken, Publisher& publisher) -> Task<std::optional<Output>> {
            co_return std::optional<Output>{fn(in, ctx, publisher)};
        });
        return *this;
    }

    template <class Fn>
    ToStage& then(Fn fn)
    {
        fragments_.push_back([fn = std::move(fn)](const Input& in, Context& ctx, CancelToken tok, Publisher& publisher) -> Task<std::optional<Output>> {
            co_return co_await fn(in, ctx, tok, publisher);
        });
        return *this;
    }

    ToStage& retry(int attempts,
                   typename traits::template Callable<Task<void>(int, const Input&, Context&, CancelToken, Publisher&)> backoff)
    {
        auto sequence = std::move(fragments_);
        fragments_.clear();
        fragments_.push_back([sequence = std::move(sequence), attempts, backoff = std::move(backoff)](const Input& in,
                                                                                                      Context& ctx,
                                                                                                      CancelToken tok,
                                                                                                      Publisher& publisher) mutable -> Task<std::optional<Output>> {
            for(int attempt = 1; attempt <= attempts; ++attempt)
            {
                std::optional<Output> result;
                for(auto& step : sequence)
                {
                    if(auto value = co_await step(in, ctx, tok, publisher))
                    {
                        result = std::move(value);
                    }
                    if(tok.stop_requested())
                    {
                        co_return std::optional<Output>{};
                    }
                }
                if(result) co_return result;
                if(attempt < attempts)
                {
                    co_await backoff(attempt, in, ctx, tok, publisher);
                }
                if(tok.stop_requested()) co_return std::optional<Output>{};
            }
            co_return std::optional<Output>{};
        });
        return *this;
    }

    void attach()
    {
        auto composed = [fragments = std::move(fragments_)](const Input& in,
                                                            Context& ctx,
                                                            CancelToken tok,
                                                            Publisher& publisher) mutable -> Task<std::optional<Output>> {
            std::optional<Output> output;
            for(auto& fragment : fragments)
            {
                if(auto value = co_await fragment(in, ctx, tok, publisher))
                {
                    output = std::move(value);
                }
                if(tok.stop_requested()) break;
            }
            co_return output;
        };

        adapter_.bind_async(from_, to_, std::move(composed));
    }

private:
    Adapter& adapter_;
    State from_;
    State to_;
    std::vector<Fragment> fragments_;
};

template <class CoMachine>
class CoBuilder
{
public:
    using State = typename CoMachine::State_t;
    using Input = typename CoMachine::Input_t;
    using Output = typename CoMachine::Output_t;
    using Context = typename CoMachine::Ctx_t;
    using Base = typename CoMachine::Builder;
    using Effect = typename CoMachine::Effect;

    Base& base() &
    {
        return builder_;
    }

    struct Plan
    {
        typename CoMachine::template Callable<void(Adapter<CoMachine>&)> attach;
    };

    class FromStage;
    template <class Event>
    class OnStage;
    class ToStage;

    FromStage from(const State& state)
    {
        return FromStage(*this, state);
    }

    struct Built
    {
        CoMachine machine;
        Adapter<CoMachine> adapter;
    };

    Built build(Context initial_ctx = {}, CancelSource* cancel_source = nullptr) &&
    {
        CoMachine machine = std::move(builder_).build(std::move(initial_ctx));
        Adapter<CoMachine> adapter(machine, cancel_source);
        for(auto& plan : plans_)
        {
            plan.attach(adapter);
        }
        return Built{std::move(machine), std::move(adapter)};
    }

    CoBuilder& set_initial(State state)
    {
        builder_.set_initial(std::move(state));
        return *this;
    }

    CoBuilder& on_enter(const State& state,
                        typename CoMachine::template Callable<void(Context&, const State&, const State&, const Input*)> fn)
    {
        builder_.on_enter(state, std::move(fn));
        return *this;
    }

    CoBuilder& on_exit(const State& state,
                       typename CoMachine::template Callable<void(Context&, const State&, const State&, const Input*)> fn)
    {
        builder_.on_exit(state, std::move(fn));
        return *this;
    }

    template <class Fn>
    CoBuilder& on_do(const State& state, Fn&& fn)
    {
        builder_.on_do(state, std::forward<Fn>(fn));
        return *this;
    }

    template <class P>
    CoBuilder& set_publisher(P&& publisher)
        requires(Effect::has_configurable_publisher)
    {
        builder_.set_publisher(std::forward<P>(publisher));
        return *this;
    }

    class FromStage
    {
    public:
        FromStage(CoBuilder& builder, State state)
            : builder_(builder), from_(state) {}

        template <class Event>
        OnStage<Event> on()
        {
            return OnStage<Event>(builder_, from_);
        }

    private:
        CoBuilder& builder_;
        State from_;
    };

    template <class Event>
    class OnStage
    {
    public:
        OnStage(CoBuilder& builder, State from)
            : builder_(builder), from_(from) {}

        ToStage to(State to)
        {
            return ToStage(builder_, from_, to);
        }

    private:
        CoBuilder& builder_;
        State from_;
    };

    class ToStage
    {
    public:
        using Fragment = typename Adapter<CoMachine>::ToStage::Fragment;

        ToStage(CoBuilder& builder, State from, State to)
            : builder_(builder), from_(from), to_(to) {}

        template <class Fn>
        ToStage& await(Fn fn)
        {
            fragments_.push_back([fn = std::move(fn)](const Input& in, Context& ctx, CancelToken tok, typename CoMachine::Publisher_t& publisher) -> Task<std::optional<Output>> {
                co_await fn(in, ctx, tok, publisher);
                co_return std::nullopt;
            });
            return *this;
        }

        template <class Fn>
        ToStage& emit(Fn fn)
        {
            fragments_.push_back([fn = std::move(fn)](const Input& in, Context& ctx, CancelToken, typename CoMachine::Publisher_t& publisher) -> Task<std::optional<Output>> {
                co_return std::optional<Output>{fn(in, ctx, publisher)};
            });
            return *this;
        }

        template <class Fn>
        ToStage& then(Fn fn)
        {
            fragments_.push_back([fn = std::move(fn)](const Input& in, Context& ctx, CancelToken tok, typename CoMachine::Publisher_t& publisher) -> Task<std::optional<Output>> {
                co_return co_await fn(in, ctx, tok, publisher);
            });
            return *this;
        }

        ToStage& retry(int attempts,
                       typename CoMachine::template Callable<Task<void>(int, const Input&, Context&, CancelToken, typename CoMachine::Publisher_t&)> backoff)
        {
            auto sequence = std::move(fragments_);
            fragments_.clear();
            fragments_.push_back([sequence = std::move(sequence), attempts, backoff = std::move(backoff)](const Input& in,
                                                                                                          Context& ctx,
                                                                                                          CancelToken tok,
                                                                                                          typename CoMachine::Publisher_t& publisher) mutable -> Task<std::optional<Output>> {
                for(int attempt = 1; attempt <= attempts; ++attempt)
                {
                    std::optional<Output> result;
                    for(auto& step : sequence)
                    {
                        if(auto value = co_await step(in, ctx, tok, publisher))
                        {
                            result = std::move(value);
                        }
                        if(tok.stop_requested())
                        {
                            co_return std::optional<Output>{};
                        }
                    }
                    if(result) co_return result;
                    if(attempt < attempts)
                    {
                        co_await backoff(attempt, in, ctx, tok, publisher);
                    }
                    if(tok.stop_requested()) co_return std::optional<Output>{};
                }
                co_return std::optional<Output>{};
            });
            return *this;
        }

        void attach()
        {
            auto composed = [fragments = std::move(fragments_)](const Input& in,
                                                                Context& ctx,
                                                                CancelToken tok,
                                                                typename CoMachine::Publisher_t& publisher) mutable -> Task<std::optional<Output>> {
                std::optional<Output> output;
                for(auto& fragment : fragments)
                {
                    if(auto value = co_await fragment(in, ctx, tok, publisher))
                    {
                        output = std::move(value);
                    }
                    if(tok.stop_requested()) break;
                }
                co_return output;
            };

            builder_.plans_.push_back(Plan{[from = from_, to = to_, composed = std::move(composed)](Adapter<CoMachine>& adapter) mutable {
                adapter.bind_async(from, to, std::move(composed));
            }});
        }

    private:
        CoBuilder& builder_;
        State from_;
        State to_;
        std::vector<Fragment> fragments_;
    };

private:
    Base builder_;
    std::vector<Plan> plans_;
};

} // namespace co

} // namespace lsm
