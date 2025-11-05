#ifndef LSM_DETAIL_EFFECT_HPP
#define LSM_DETAIL_EFFECT_HPP

#include <concepts>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include <lsm/detail/concepts.hpp>
#include <lsm/detail/policy.hpp>

namespace lsm
{
namespace detail
{

template <class T>
struct always_false : std::false_type
{
};

struct no_action_t
{
};
struct no_guard_t
{
};

inline constexpr no_action_t no_action{};
inline constexpr no_guard_t no_guard{};

struct NullPublisher
{
    template <class T>
    void publish(T&&) const noexcept {}
};

template <class Storage>
class PublisherQueue
{
public:
    using storage_type = Storage;
    using value_type = typename Storage::value_type;

    PublisherQueue() = default;
    explicit PublisherQueue(Storage& storage) noexcept : storage_(&storage) {}

    template <class T>
    void publish(T&& value)
    {
        if(storage_)
        {
            storage_->push_back(std::forward<T>(value));
        }
    }

private:
    Storage* storage_ = nullptr;
};

template <class Publisher, class Value>
concept PublisherFor = requires(Publisher& pub, Value&& value) {
    pub.publish(std::forward<Value>(value));
};

template <class F, class Input, class Ctx, class Output>
concept ReturnActionForEx =
    requires(F f, const Input& in, Ctx& ctx) {
        { f(in, ctx) } -> std::convertible_to<std::optional<Output>>;
    };

template <class F, class Input, class Ctx, class Publisher>
concept PublisherActionForEx =
    requires(F f, const Input& in, Ctx& ctx, Publisher& pub) {
        { f(in, ctx, pub) } -> std::same_as<void>;
    };

template <class F, class Ctx, class State, class Output>
concept ReturnStateActionForEx =
    requires(F f, Ctx& ctx, const State& state) {
        { f(ctx, state) } -> std::convertible_to<std::optional<Output>>;
    };

template <class F, class Ctx, class State, class Publisher>
concept PublisherStateActionForEx =
    requires(F f, Ctx& ctx, const State& state, Publisher& pub) {
        { f(ctx, state, pub) } -> std::same_as<void>;
    };

template <class F, class Ctx, class Output>
concept ReturnCompletionActionForEx =
    requires(F f, Ctx& ctx) {
        { f(ctx) } -> std::convertible_to<std::optional<Output>>;
    };

template <class F, class Ctx, class Publisher>
concept PublisherCompletionActionForEx =
    requires(F f, Ctx& ctx, Publisher& pub) {
        { f(ctx, pub) } -> std::same_as<void>;
    };

template <class EffectPolicy, class CallablePolicy, class State, class Input, class Output, class Context>
struct EffectBindings;

template <class Output, class CallablePolicy, class State, class Input, class Context>
struct EffectBindings<policy::ReturnOutput<Output>, CallablePolicy, State, Input, Output, Context>
{
    using policy_type = policy::ReturnOutput<Output>;
    using Action = typename CallablePolicy::template Callable<std::optional<Output>(const Input&, Context&)>;

    template <class Event>
    using TypedAction = typename CallablePolicy::template Callable<std::optional<Output>(const Event&, Context&)>;

    using StateAction = typename CallablePolicy::template Callable<std::optional<Output>(Context&, const State&)>;
    using CompletionAction = typename CallablePolicy::template Callable<std::optional<Output>(Context&)>;
    using Publisher = NullPublisher;
    using PublisherStorage = NullPublisher;
    static constexpr bool has_configurable_publisher = false;

    template <class Fn>
    static Action bind_action(Fn&& fn)
    {
        using Fn_t = std::decay_t<Fn>;
        if constexpr(std::is_same_v<Fn_t, no_action_t>)
        {
            return Action{};
        }
        else
        {
            static_assert(ReturnActionForEx<Fn_t, Input, Context, Output>,
                          "Action must return std::optional<Output>(const Input&, Ctx&)");
            return Action{std::forward<Fn>(fn)};
        }
    }

    template <class Event, class Fn>
    static TypedAction<Event> bind_typed_action(Fn&& fn)
    {
        using Fn_t = std::decay_t<Fn>;
        if constexpr(std::is_same_v<Fn_t, no_action_t>)
        {
            return TypedAction<Event>{};
        }
        else
        {
            static_assert(ReturnActionForEx<Fn_t, Event, Context, Output>,
                          "Typed action must return std::optional<Output>(const Event&, Ctx&)");
            return TypedAction<Event>{std::forward<Fn>(fn)};
        }
    }

    template <class Event>
    static Action lift_variant_action(TypedAction<Event>&& action)
    {
        if(!action) return Action{};
        return Action{
            [act = std::move(action)](const Input& in, Context& ctx) mutable -> std::optional<Output> {
                return act(std::get<Event>(in), ctx);
            }};
    }

    template <class Fn>
    static StateAction bind_state_action(Fn&& fn)
    {
        using Fn_t = std::decay_t<Fn>;
        if constexpr(std::is_same_v<Fn_t, no_action_t>)
        {
            return StateAction{};
        }
        else
        {
            static_assert(ReturnStateActionForEx<Fn_t, Context, State, Output>,
                          "on_do must return std::optional<Output>(Ctx&, const State&)");
            return StateAction{std::forward<Fn>(fn)};
        }
    }

    template <class Fn>
    static CompletionAction bind_completion_action(Fn&& fn)
    {
        using Fn_t = std::decay_t<Fn>;
        if constexpr(std::is_same_v<Fn_t, no_action_t>)
        {
            return CompletionAction{};
        }
        else
        {
            static_assert(ReturnCompletionActionForEx<Fn_t, Context, Output>,
                          "Completion action must return std::optional<Output>(Ctx&)");
            return CompletionAction{std::forward<Fn>(fn)};
        }
    }

    template <class Machine>
    static std::optional<Output> invoke_transition_action(Machine&, Action& action,
                                                          const Input& in, Context& ctx)
    {
        if(action) return action(in, ctx);
        return std::nullopt;
    }

    template <class Machine>
    static std::optional<Output> invoke_state_action(Machine&, StateAction& action,
                                                     Context& ctx, const State& state)
    {
        if(action) return action(ctx, state);
        return std::nullopt;
    }

    template <class Machine>
    static std::optional<Output> invoke_completion_action(Machine&, CompletionAction& action,
                                                          Context& ctx)
    {
        if(action) return action(ctx);
        return std::nullopt;
    }

    static PublisherStorage default_publisher()
    {
        return PublisherStorage{};
    }

    template <class P>
    static PublisherStorage make_publisher_storage(P&&)
    {
        static_assert(always_false<P>::value,
                      "ReturnOutput policy does not accept external publisher storage");
        return PublisherStorage{};
    }
};

template <class Pub, class CallablePolicy, class State, class Input, class Output, class Context>
struct EffectBindings<policy::Publisher<Pub>, CallablePolicy, State, Input, Output, Context>
{
    using policy_type = policy::Publisher<Pub>;
    using Publisher = Pub;
    using Action = typename CallablePolicy::template Callable<void(const Input&, Context&, Publisher&)>;

    template <class Event>
    using TypedAction = typename CallablePolicy::template Callable<void(const Event&, Context&, Publisher&)>;

    using StateAction = typename CallablePolicy::template Callable<void(Context&, const State&, Publisher&)>;
    using CompletionAction = typename CallablePolicy::template Callable<void(Context&, Publisher&)>;
    using PublisherStorage = Publisher;
    static constexpr bool has_configurable_publisher = true;

    template <class Fn>
    static Action bind_action(Fn&& fn)
    {
        using Fn_t = std::decay_t<Fn>;
        if constexpr(std::is_same_v<Fn_t, no_action_t>)
        {
            return Action{};
        }
        else
        {
            static_assert(PublisherActionForEx<Fn_t, Input, Context, Publisher>,
                          "Action must be void(const Input&, Ctx&, Publisher&)");
            return Action{std::forward<Fn>(fn)};
        }
    }

    template <class Event, class Fn>
    static TypedAction<Event> bind_typed_action(Fn&& fn)
    {
        using Fn_t = std::decay_t<Fn>;
        if constexpr(std::is_same_v<Fn_t, no_action_t>)
        {
            return TypedAction<Event>{};
        }
        else
        {
            static_assert(PublisherActionForEx<Fn_t, Event, Context, Publisher>,
                          "Typed action must be void(const Event&, Ctx&, Publisher&)");
            return TypedAction<Event>{std::forward<Fn>(fn)};
        }
    }

    template <class Event>
    static Action lift_variant_action(TypedAction<Event>&& action)
    {
        if(!action) return Action{};
        return Action{
            [act = std::move(action)](const Input& in, Context& ctx, Publisher& publisher) mutable {
                act(std::get<Event>(in), ctx, publisher);
            }};
    }

    template <class Fn>
    static StateAction bind_state_action(Fn&& fn)
    {
        using Fn_t = std::decay_t<Fn>;
        if constexpr(std::is_same_v<Fn_t, no_action_t>)
        {
            return StateAction{};
        }
        else
        {
            static_assert(PublisherStateActionForEx<Fn_t, Context, State, Publisher>,
                          "on_do must be void(Ctx&, const State&, Publisher&)");
            return StateAction{std::forward<Fn>(fn)};
        }
    }

    template <class Fn>
    static CompletionAction bind_completion_action(Fn&& fn)
    {
        using Fn_t = std::decay_t<Fn>;
        if constexpr(std::is_same_v<Fn_t, no_action_t>)
        {
            return CompletionAction{};
        }
        else
        {
            static_assert(PublisherCompletionActionForEx<Fn_t, Context, Publisher>,
                          "Completion action must be void(Ctx&, Publisher&)");
            return CompletionAction{std::forward<Fn>(fn)};
        }
    }

    template <class Machine>
    static std::optional<Output> invoke_transition_action(Machine& machine, Action& action,
                                                          const Input& in, Context& ctx)
    {
        if(action) action(in, ctx, machine.publisher());
        return std::nullopt;
    }

    template <class Machine>
    static std::optional<Output> invoke_state_action(Machine& machine, StateAction& action,
                                                     Context& ctx, const State& state)
    {
        if(action) action(ctx, state, machine.publisher());
        return std::nullopt;
    }

    template <class Machine>
    static std::optional<Output> invoke_completion_action(Machine& machine, CompletionAction& action,
                                                          Context& ctx)
    {
        if(action) action(ctx, machine.publisher());
        return std::nullopt;
    }

    static PublisherStorage default_publisher()
    {
        if constexpr(std::is_default_constructible_v<PublisherStorage>)
        {
            return PublisherStorage{};
        }
        else
        {
            static_assert(always_false<PublisherStorage>::value,
                          "Publisher policy requires Builder::set_publisher()");
            return PublisherStorage{};
        }
    }

    template <class P>
    static PublisherStorage make_publisher_storage(P&& pub)
    {
        return PublisherStorage{std::forward<P>(pub)};
    }
};

} // namespace detail

namespace publisher
{
template <class Publisher, class Value>
concept Concept = detail::PublisherFor<Publisher, Value>;

using NullPublisher = detail::NullPublisher;

template <class Storage>
using Queue = detail::PublisherQueue<Storage>;
} // namespace publisher

} // namespace lsm

#endif
