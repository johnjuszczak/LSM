#ifndef LSM_DETAIL_MACHINE_IMPL_HPP
#define LSM_DETAIL_MACHINE_IMPL_HPP

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <lsm/detail/concepts.hpp>
#include <lsm/detail/effect.hpp>
#include <lsm/detail/handlers.hpp>
#include <lsm/detail/policy.hpp>
#include <lsm/detail/types.hpp>

namespace lsm
{

template <typename State,
          typename Input,
          typename Output = std::monostate,
          typename Context = std::monostate,
          PolicyHasCallableTemplate CallablePolicy = policy::copy,
          typename EffectPolicy = policy::ReturnOutput<Output>>
class MachineImpl
{
public:
    template <typename Sig>
    using Callable = typename CallablePolicy::template Callable<Sig>;

    using State_t = State;
    using Input_t = Input;
    using Output_t = Output;
    using Ctx_t = Context;

    using Effect = detail::EffectBindings<EffectPolicy, CallablePolicy, State_t, Input_t, Output_t, Ctx_t>;
    using StateHandlers = detail::StateHandlers<State_t, Input_t, Output_t, Ctx_t, CallablePolicy, Effect>;
    using Transition = detail::Transition<State_t, Input_t, Output_t, Ctx_t, CallablePolicy, Effect>;
    using Completion = detail::CompletionTransition<State_t, Input_t, Output_t, Ctx_t, CallablePolicy, Effect>;
    using Guard = typename Transition::Guard;
    using Action = typename Transition::Action;
    using CompletionGuard = typename Completion::Guard;
    using CompletionAction = typename Completion::Action;
    using AnyState_t = detail::AnyState_t;
    using Policy = CallablePolicy;
    using Publisher_t = typename Effect::PublisherStorage;

    static constexpr AnyState_t AnyState{};

    class Selection
    {
        friend class MachineImpl;
        const Transition* transition_ = nullptr;
        bool valid_ = false;
        bool deferred_ = false;
        Selection() = default;
        explicit Selection(const Transition* t)
            : transition_(t), valid_(t != nullptr), deferred_(t ? t->defer : false) {}

    public:
        explicit operator bool() const noexcept
        {
            return valid_;
        }
        bool deferred() const noexcept
        {
            return deferred_;
        }
        const Transition* get() const noexcept
        {
            return transition_;
        }
    };

    class Builder
    {
    public:
        using Policy = CallablePolicy;
        Builder& set_initial(State_t s)
        {
            initial_ = std::move(s);
            return *this;
        }

        Builder& enable_deferral(bool v = true)
        {
            deferral_enabled_ = v;
            return *this;
        }

        template <class P>
        Builder& set_publisher(P&& publisher)
            requires(Effect::has_configurable_publisher)
        {
            publisher_ = Effect::make_publisher_storage(std::forward<P>(publisher));
            return *this;
        }

        Builder& on_enter(const State_t& s, Callable<void(Ctx_t&, const State_t&, const State_t&, const Input_t*)> fn)
        {
            states_[s].on_enter = std::move(fn);
            return *this;
        }

        Builder& on_exit(const State_t& s, Callable<void(Ctx_t&, const State_t&, const State_t&, const Input_t*)> fn)
        {
            states_[s].on_exit = std::move(fn);
            return *this;
        }

        template <class Fn = detail::no_action_t>
        Builder& on_do(const State_t& s, Fn&& fn = {})
        {
            states_[s].on_do = Effect::bind_state_action(std::forward<Fn>(fn));
            return *this;
        }

        Builder& on_unhandled(Callable<void(Ctx_t&, const State_t&, const Input_t&)> fn)
        {
            unhandled_ = std::move(fn);
            return *this;
        }

        Builder& on_unhandled(const State_t& s, Callable<void(Ctx_t&, const State_t&, const Input_t&)> fn)
        {
            states_[s].on_unhandled = std::move(fn);
            return *this;
        }

        // Convenience: default to by_ref binding when tag omitted
        template <class Handler>
            requires StateHandlerFor<Handler, State_t, Input_t, Output_t, Ctx_t, EffectPolicy>
        Builder& on_state(const State_t& s, Handler& h)
        {
            return on_state(s, h, detail::by_ref{});
        }

        // Object-centric handler binding
        template <class Handler>
            requires StateHandlerFor<Handler, State_t, Input_t, Output_t, Ctx_t, EffectPolicy>
        Builder& on_state(const State_t& s, Handler& h, detail::by_ref)
        {
            using SH = StateHandlers;
            if(auto enter = detail::bind_on_enter<SH>(h, detail::by_ref{}))
            {
                states_[s].on_enter = std::move(enter);
            }
            if(auto exitf = detail::bind_on_exit<SH>(h, detail::by_ref{}))
            {
                states_[s].on_exit = std::move(exitf);
            }
            if(auto ondo = detail::bind_on_do<SH>(h, detail::by_ref{}))
            {
                states_[s].on_do = std::move(ondo);
            }
            return *this;
        }

        template <class Handler>
            requires StateHandlerFor<std::remove_pointer_t<Handler>, State_t, Input_t, Output_t, Ctx_t, EffectPolicy>
        Builder& on_state(const State_t& s, Handler* h, detail::by_ptr)
        {
            using SH = StateHandlers;
            if(auto enter = detail::bind_on_enter<SH>(h, detail::by_ptr{}))
            {
                states_[s].on_enter = std::move(enter);
            }
            if(auto exitf = detail::bind_on_exit<SH>(h, detail::by_ptr{}))
            {
                states_[s].on_exit = std::move(exitf);
            }
            if(auto ondo = detail::bind_on_do<SH>(h, detail::by_ptr{}))
            {
                states_[s].on_do = std::move(ondo);
            }
            return *this;
        }

        template <class Handler>
            requires StateHandlerFor<Handler, State_t, Input_t, Output_t, Ctx_t, EffectPolicy>
        Builder& on_state(const State_t& s, std::shared_ptr<Handler> h, detail::by_shared)
        {
            using SH = StateHandlers;
            if(auto enter = detail::bind_on_enter<SH>(h, detail::by_shared{}))
            {
                states_[s].on_enter = std::move(enter);
            }
            if(auto exitf = detail::bind_on_exit<SH>(h, detail::by_shared{}))
            {
                states_[s].on_exit = std::move(exitf);
            }
            if(auto ondo = detail::bind_on_do<SH>(h, detail::by_shared{}))
            {
                states_[s].on_do = std::move(ondo);
            }
            return *this;
        }

        Builder& add_transition(Transition t)
        {
            trans_[t.from].push_back(std::move(t));
            return *this;
        }
        Builder& add_transition(AnyState_t, Transition t)
        {
            any_.push_back(std::move(t));
            return *this;
        }
        Builder& add_completion(Completion t)
        {
            completions_[t.from].push_back(std::move(t));
            return *this;
        }

        template <class T,
                  class ActionFn = detail::no_action_t,
                  class GuardFn = detail::no_guard_t>
            requires IsVariant<Input_t>
        Builder& on(const State_t& from, const State_t& to,
                    ActionFn action_fn = {},
                    GuardFn guard_fn = {},
                    int priority = 0,
                    bool suppress_enter_exit = false,
                    bool defer = false)
        {
            Transition tr = make_transition(from, to, priority, suppress_enter_exit, defer);
            auto type_guard = make_variant_guard<T>();
            auto extra_guard = make_guard(std::move(guard_fn));
            tr.guard = combine_guards(std::move(type_guard), std::move(extra_guard));
            auto typed_action = make_variant_action<T>(std::move(action_fn));
            tr.action = lift_variant_action<T>(std::move(typed_action));
            return add_transition(std::move(tr));
        }

        template <class ActionFn = detail::no_action_t,
                  class GuardFn = detail::no_guard_t>
        Builder& on_value(const State_t& from, const State_t& to, Input_t value,
                          ActionFn action_fn = {},
                          GuardFn guard_fn = {},
                          int priority = 0,
                          bool suppress_enter_exit = false,
                          bool defer = false)
            requires EqComparable<Input_t>
        {
            Transition tr = make_transition(from, to, priority, suppress_enter_exit, defer);
            auto base_guard = make_value_guard(std::move(value));
            tr.guard = combine_guards(std::move(base_guard), make_guard(std::move(guard_fn)));
            tr.action = make_input_action(std::move(action_fn));
            return add_transition(std::move(tr));
        }

        template <class T,
                  class ActionFn = detail::no_action_t,
                  class GuardFn = detail::no_guard_t>
            requires IsVariant<Input_t>
        Builder& on_any(const State_t& to,
                        ActionFn action_fn = {},
                        GuardFn guard_fn = {},
                        int priority = 0,
                        bool suppress_enter_exit = false,
                        bool defer = false)
        {
            Transition tr = make_any_transition(to, priority, suppress_enter_exit, defer);
            auto type_guard = make_variant_guard<T>();
            tr.guard = combine_guards(std::move(type_guard), make_guard(std::move(guard_fn)));
            auto typed_action = make_variant_action<T>(std::move(action_fn));
            tr.action = lift_variant_action<T>(std::move(typed_action));
            any_.push_back(std::move(tr));
            return *this;
        }

        template <class ActionFn = detail::no_action_t,
                  class GuardFn = detail::no_guard_t>
        Builder& on_any_value(const State_t& to, Input_t value,
                              ActionFn action_fn = {},
                              GuardFn guard_fn = {},
                              int priority = 0,
                              bool suppress_enter_exit = false,
                              bool defer = false)
            requires EqComparable<Input_t>
        {
            Transition tr = make_any_transition(to, priority, suppress_enter_exit, defer);
            auto base_guard = make_value_guard(std::move(value));
            tr.guard = combine_guards(std::move(base_guard), make_guard(std::move(guard_fn)));
            tr.action = make_input_action(std::move(action_fn));
            any_.push_back(std::move(tr));
            return *this;
        }

        template <class ActionFn = detail::no_action_t>
        Builder& on_completion(const State_t& from, const State_t& to,
                               ActionFn action_fn = {},
                               bool suppress_enter_exit = false,
                               int priority = 0,
                               CompletionGuard guard = {})
        {
            Completion comp;
            comp.from = from;
            comp.to = to;
            comp.suppress_enter_exit = suppress_enter_exit;
            comp.priority = priority;
            comp.action = Effect::bind_completion_action(std::forward<ActionFn>(action_fn));
            comp.guard = std::move(guard);
            return add_completion(std::move(comp));
        }

        template <class T>
        class OnTypeStage;
        class OnValueStage;
        class FromStage;
        class AnyStage;
        class CompletionStage;

        FromStage from(const State_t& s)
        {
            return FromStage(*this, s);
        }
        AnyStage any()
        {
            return AnyStage(*this);
        }
        CompletionStage completion(const State_t& s)
        {
            return CompletionStage(*this, s);
        }

        MachineImpl build(Ctx_t initial_ctx = {}) &&
        {
            auto cmp = [](const Transition& a, const Transition& b) {
                return a.priority > b.priority;
            };
            for(auto& [st, vec] : trans_)
            {
                std::stable_sort(vec.begin(), vec.end(), cmp);
            }
            std::stable_sort(any_.begin(), any_.end(), cmp);

            auto cmp_completion = [](const Completion& a, const Completion& b) {
                return a.priority > b.priority;
            };
            for(auto& [st, vec] : completions_)
            {
                std::stable_sort(vec.begin(), vec.end(), cmp_completion);
            }

            return MachineImpl(std::move(initial_), std::move(states_),
                               std::move(trans_), std::move(any_), std::move(completions_), std::move(initial_ctx),
                               std::move(unhandled_), take_publisher(), deferral_enabled_);
        }

        class FromStage
        {
        public:
            FromStage(Builder& b, const State_t& s) : b_(b), from_(s) {}
            template <class T>
                requires IsVariant<Input_t>
            OnTypeStage<T> on()
            {
                return OnTypeStage<T>(b_, &from_, nullptr);
            }
            template <class T>
                requires IsVariant<Input_t>
            OnTypeStage<T> on(std::type_identity<T>)
            {
                return OnTypeStage<T>(b_, &from_, nullptr);
            }
            OnValueStage on_value(Input_t value)
            {
                return OnValueStage(b_, &from_, nullptr, std::move(value));
            }

        private:
            Builder& b_;
            State_t from_;
        };

        class AnyStage
        {
        public:
            explicit AnyStage(Builder& b) : b_(b) {}
            template <class T>
                requires IsVariant<Input_t>
            OnTypeStage<T> on()
            {
                return OnTypeStage<T>(b_, nullptr, &AnyState);
            }
            template <class T>
                requires IsVariant<Input_t>
            OnTypeStage<T> on(std::type_identity<T>)
            {
                return OnTypeStage<T>(b_, nullptr, &AnyState);
            }
            OnValueStage on_value(Input_t value)
            {
                return OnValueStage(b_, nullptr, &AnyState, std::move(value));
            }

        private:
            Builder& b_;
        };

        template <class T>
        class OnTypeStage
        {
        public:
            OnTypeStage(Builder& b, const State_t* from, const AnyState_t* any)
                : b_(b), from_(from), any_(any)
            {
                static_assert(IsVariant<Input_t>, "fluent .on<T>() requires Input to be std::variant<...>");
            }

            template <class Fn>
            OnTypeStage& action(Fn&& fn)
            {
                action_T_ = Builder::template make_variant_action<T>(std::forward<Fn>(fn));
                return *this;
            }

            template <class Fn>
            OnTypeStage& guard(Fn&& fn)
            {
                guard_ = Builder::make_guard(std::forward<Fn>(fn));
                return *this;
            }

            OnTypeStage& priority(int p)
            {
                priority_ = p;
                return *this;
            }
            OnTypeStage& suppress_enter_exit(bool v = true)
            {
                suppress_enter_exit_ = v;
                return *this;
            }
            OnTypeStage& defer(bool v = true)
            {
                defer_ = v;
                return *this;
            }

            OnTypeStage& to(const State_t& to)
            {
                if(from_)
                {
                    b_.on<T>(*from_, to,
                             std::move(action_T_),
                             std::move(guard_),
                             priority_,
                             suppress_enter_exit_,
                             defer_);
                }
                else
                {
                    b_.on_any<T>(to,
                                 std::move(action_T_),
                                 std::move(guard_),
                                 priority_,
                                 suppress_enter_exit_,
                                 defer_);
                }
                return *this;
            }

        private:
            Builder& b_;
            const State_t* from_ = nullptr;
            const AnyState_t* any_ = nullptr;
            int priority_ = 0;
            bool suppress_enter_exit_ = false;
            using TypedAction = typename Effect::template TypedAction<T>;
            TypedAction action_T_{};
            Guard guard_{};
            bool defer_ = false;
        };

        class OnValueStage
        {
        public:
            OnValueStage(Builder& b, const State_t* from, const AnyState_t* any, Input_t value)
                : b_(b), from_(from), any_(any), value_(std::move(value)) {}

            template <class Fn>
            OnValueStage& action(Fn&& fn)
            {
                action_ = Builder::make_input_action(std::forward<Fn>(fn));
                return *this;
            }

            template <class Fn>
            OnValueStage& guard(Fn&& fn)
            {
                guard_ = Builder::make_guard(std::forward<Fn>(fn));
                return *this;
            }

            OnValueStage& priority(int p)
            {
                priority_ = p;
                return *this;
            }
            OnValueStage& suppress_enter_exit(bool v = true)
            {
                suppress_enter_exit_ = v;
                return *this;
            }
            OnValueStage& defer(bool v = true)
            {
                defer_ = v;
                return *this;
            }

            OnValueStage& to(const State_t& to)
            {
                if(from_)
                    b_.on_value(*from_, to, std::move(value_), std::move(action_), std::move(guard_), priority_, suppress_enter_exit_, defer_);
                else
                    b_.on_any_value(to, std::move(value_), std::move(action_), std::move(guard_), priority_, suppress_enter_exit_, defer_);
                return *this;
            }

        private:
            Builder& b_;
            const State_t* from_ = nullptr;
            const AnyState_t* any_ = nullptr;
            Input_t value_;
            Action action_{};
            int priority_ = 0;
            bool suppress_enter_exit_ = false;
            Guard guard_{};
            bool defer_ = false;
        };

        class CompletionStage
        {
        public:
            CompletionStage(Builder& b, const State_t& from)
                : b_(b), from_(from) {}

            CompletionStage& action(CompletionAction a)
            {
                action_ = std::move(a);
                return *this;
            }
            CompletionStage& guard(CompletionGuard g)
            {
                guard_ = std::move(g);
                return *this;
            }
            CompletionStage& suppress_enter_exit(bool v = true)
            {
                suppress_enter_exit_ = v;
                return *this;
            }
            CompletionStage& priority(int p)
            {
                priority_ = p;
                return *this;
            }

            Builder& to(const State_t& to)
            {
                Completion comp;
                comp.from = from_;
                comp.to = to;
                comp.suppress_enter_exit = suppress_enter_exit_;
                comp.priority = priority_;
                comp.action = std::move(action_);
                comp.guard = std::move(guard_);
                return b_.add_completion(std::move(comp));
            }

        private:
            Builder& b_;
            State_t from_;
            CompletionAction action_{};
            CompletionGuard guard_{};
            bool suppress_enter_exit_ = false;
            int priority_ = 0;
        };

    private:
        template <class>
        friend class OnTypeStage;
        friend class OnValueStage;

        static Transition make_transition(const State_t& from,
                                          const State_t& to,
                                          int priority,
                                          bool suppress_enter_exit,
                                          bool defer)
        {
            Transition tr;
            tr.from = from;
            tr.to = to;
            tr.priority = priority;
            tr.suppress_enter_exit = suppress_enter_exit;
            tr.defer = defer;
            return tr;
        }

        static Transition make_any_transition(const State_t& to,
                                              int priority,
                                              bool suppress_enter_exit,
                                              bool defer)
        {
            Transition tr;
            tr.to = to;
            tr.priority = priority;
            tr.suppress_enter_exit = suppress_enter_exit;
            tr.defer = defer;
            return tr;
        }

        template <class Fn>
        static Action make_input_action(Fn&& fn)
        {
            return Effect::bind_action(std::forward<Fn>(fn));
        }

        template <class Event, class Fn>
        static auto make_variant_action(Fn&& fn) -> typename Effect::template TypedAction<Event>
        {
            return Effect::template bind_typed_action<Event>(std::forward<Fn>(fn));
        }

        template <class Event>
        static Action lift_variant_action(typename Effect::template TypedAction<Event>&& action)
        {
            return Effect::template lift_variant_action<Event>(std::move(action));
        }

        template <class Fn>
        static Guard make_guard(Fn&& fn)
        {
            using Fn_t = std::decay_t<Fn>;
            if constexpr(std::is_same_v<Fn_t, detail::no_guard_t> || std::is_same_v<Fn_t, std::nullptr_t>)
            {
                return Guard{};
            }
            else if constexpr(std::is_same_v<Fn_t, Guard>)
            {
                return std::forward<Fn>(fn);
            }
            else
            {
                static_assert(GuardFor<Fn_t, Input_t, Ctx_t>,
                              "guard must satisfy GuardFor<Input_t, Context>");
                return Guard{std::forward<Fn>(fn)};
            }
        }

        template <class Event>
        static Guard make_variant_guard()
        {
            return Guard{[](const Input_t& in, const Ctx_t&) { return std::holds_alternative<Event>(in); }};
        }

        static Guard make_value_guard(Input_t value)
        {
            return Guard{[value = std::move(value)](const Input_t& in, const Ctx_t&) { return in == value; }};
        }

        static Guard combine_guards(Guard primary, Guard extra)
        {
            const bool has_primary = static_cast<bool>(primary);
            const bool has_extra = static_cast<bool>(extra);
            if(has_primary && has_extra)
            {
                return Guard{
                    [primary = std::move(primary), extra = std::move(extra)](const Input_t& in, const Ctx_t& ctx) mutable {
                        return primary(in, ctx) && extra(in, ctx);
                    }};
            }
            return has_primary ? std::move(primary) : std::move(extra);
        }

        Publisher_t take_publisher()
        {
            if(publisher_)
            {
                return std::move(*publisher_);
            }
            return Effect::default_publisher();
        }

        State_t initial_{};
        std::unordered_map<State_t, StateHandlers> states_;
        std::unordered_map<State_t, std::vector<Transition>> trans_;
        std::vector<Transition> any_;
        std::unordered_map<State_t, std::vector<Completion>> completions_;
        Callable<void(Ctx_t&, const State_t&, const Input_t&)> unhandled_{};
        bool deferral_enabled_ = false;
        std::optional<Publisher_t> publisher_{};
    };

    Selection select(const Input_t& in) const
    {
        return Selection{find_transition(in)};
    }

    std::optional<Output_t> commit(const Selection& sel, const Input_t* inptr)
    {
        if(!sel) return std::nullopt;
        const auto* t = sel.get();
        if(deferral_enabled_ && t->defer && inptr)
        {
            deferrals_[t->to].push_back(*inptr);
            apply_transition(*t, inptr, false);
            return finalize_transition(std::nullopt);
        }
        auto out = apply_transition(*t, inptr);
        return finalize_transition(std::move(out));
    }

    std::optional<Output_t> dispatch(const Input_t& in)
    {
        auto sel = select(in);
        if(sel)
        {
            return commit(sel, &in);
        }
        try
        {
            if(auto it = handlers_.find(current_); it != handlers_.end())
            {
                if(it->second.on_unhandled)
                {
                    it->second.on_unhandled(ctx_, current_, in);
                    return std::nullopt;
                }
            }
            if(machine_unhandled_)
            {
                machine_unhandled_(ctx_, current_, in);
            }
        } catch(...)
        {
        }
        return std::nullopt;
    }

    void enqueue(const Input_t& in)
    {
        pending_inputs_.push_back(in);
    }

    void enqueue(Input_t&& in)
    {
        pending_inputs_.push_back(std::move(in));
    }

    std::vector<Output_t> dispatch_all()
    {
        std::vector<Output_t> outputs;
        while(!pending_inputs_.empty())
        {
            Input_t next = std::move(pending_inputs_.front());
            pending_inputs_.pop_front();
            if(auto out = handle_input(next))
            {
                outputs.push_back(std::move(*out));
            }
        }
        return outputs;
    }

    std::optional<Output_t> update()
    {
        if(auto it = handlers_.find(current_); it != handlers_.end())
        {
            return Effect::invoke_state_action(*this, it->second.on_do, ctx_, current_);
        }
        return std::nullopt;
    }

    const State_t& state() const noexcept
    {
        return current_;
    }
    Ctx_t& context() noexcept
    {
        return ctx_;
    }
    const Ctx_t& context() const noexcept
    {
        return ctx_;
    }
    Publisher_t& publisher() noexcept
    {
        return publisher_;
    }
    const Publisher_t& publisher() const noexcept
    {
        return publisher_;
    }
    void set_state_direct(State_t next)
    {
        current_ = std::move(next);
    }

    const auto& handlers_table() const noexcept
    {
        return handlers_;
    }
    auto& handlers_table() noexcept
    {
        return handlers_;
    }
    const auto& transitions_table() const noexcept
    {
        return transitions_;
    }
    const auto& any_transitions_table() const noexcept
    {
        return any_transitions_;
    }
    const auto& completions_table() const noexcept
    {
        return completion_transitions_;
    }

    void begin_async_effect()
    {
        async_inflight_ = true;
    }
    void end_async_effect()
    {
        async_inflight_ = false;
    }
    bool async_state() const
    {
        return async_inflight_;
    }

private:
    MachineImpl(State_t init,
                std::unordered_map<State_t, StateHandlers> handlers,
                std::unordered_map<State_t, std::vector<Transition>> transitions,
                std::vector<Transition> any,
                std::unordered_map<State_t, std::vector<Completion>> completions,
                Ctx_t ctx,
                Callable<void(Ctx_t&, const State_t&, const Input_t&)> unhandled,
                Publisher_t publisher,
                bool deferral_enabled)
        : current_(init), handlers_(std::move(handlers)), transitions_(std::move(transitions)), any_transitions_(std::move(any)), completion_transitions_(std::move(completions)), ctx_(std::move(ctx)), machine_unhandled_(std::move(unhandled)), publisher_(std::move(publisher)), deferral_enabled_(deferral_enabled)
    {
        if(auto it = handlers_.find(current_); it != handlers_.end())
        {
            if(it->second.on_enter) it->second.on_enter(ctx_, current_, current_, nullptr);
        }
        completion_limit_ = 0;
        for(const auto& entry : completion_transitions_)
        {
            completion_limit_ += entry.second.size();
        }
        if(completion_limit_)
        {
            completion_limit_ += 1;
        }
        finalize_transition(std::nullopt);
    }

    std::optional<Output_t> handle_input(const Input_t& in)
    {
        if(const auto* transition = find_transition(in))
        {
            if(deferral_enabled_ && transition->defer)
            {
                deferrals_[transition->to].push_back(in);
                apply_transition(*transition, &in, false);
                return finalize_transition(std::nullopt);
            }
            auto out = apply_transition(*transition, &in);
            return finalize_transition(std::move(out));
        }

        try
        {
            if(auto it = handlers_.find(current_); it != handlers_.end())
            {
                if(it->second.on_unhandled)
                {
                    it->second.on_unhandled(ctx_, current_, in);
                    return std::nullopt;
                }
            }
            if(machine_unhandled_)
            {
                machine_unhandled_(ctx_, current_, in);
            }
        } catch(...)
        {
        }
        return std::nullopt;
    }

    const Transition* find_transition(const Input_t& input) const
    {
        const auto& ctx = context();
        const auto& current = state();

        const auto& tmap = transitions_table();
        if(auto it = tmap.find(current); it != tmap.end())
        {
            for(const auto& candidate : it->second)
            {
                if(!candidate.guard || candidate.guard(input, ctx))
                {
                    return &candidate;
                }
            }
        }

        const auto& any = any_transitions_table();
        for(const auto& candidate : any)
        {
            if(!candidate.guard || candidate.guard(input, ctx))
            {
                return &candidate;
            }
        }

        return nullptr;
    }

    std::optional<Output_t> apply_transition(const Transition& transition,
                                             const Input_t* input,
                                             bool invoke_action = true)
    {
        auto& ctx = context();
        auto& table = handlers_table();

        const auto from = state();
        const auto to = transition.to;
        const bool skip_hooks = transition.suppress_enter_exit && to == from;

        if(!skip_hooks)
        {
            if(auto it = table.find(from); it != table.end())
            {
                if(it->second.on_exit)
                {
                    it->second.on_exit(ctx, from, to, input);
                }
            }
        }

        std::optional<Output_t> output;
        if(invoke_action && input)
        {
            output = Effect::invoke_transition_action(*this, transition.action, *input, ctx);
        }

        set_state_direct(to);

        if(!skip_hooks)
        {
            if(auto it = table.find(to); it != table.end())
            {
                if(it->second.on_enter)
                {
                    it->second.on_enter(ctx, from, to, input);
                }
            }
        }

        return output;
    }

    const Completion* find_completion() const
    {
        const auto& ctx = context();
        const auto& current = state();

        const auto& cmap = completions_table();
        if(auto it = cmap.find(current); it != cmap.end())
        {
            for(const auto& candidate : it->second)
            {
                if(!candidate.guard || candidate.guard(ctx))
                {
                    return &candidate;
                }
            }
        }

        return nullptr;
    }

    std::optional<Output_t> apply_completion(const Completion& completion)
    {
        auto& ctx = context();
        auto& table = handlers_table();

        const auto from = state();
        const auto to = completion.to;
        const bool skip_hooks = completion.suppress_enter_exit && to == from;

        if(!skip_hooks)
        {
            if(auto it = table.find(from); it != table.end())
            {
                if(it->second.on_exit)
                {
                    it->second.on_exit(ctx, from, to, nullptr);
                }
            }
        }

        std::optional<Output_t> output = Effect::invoke_completion_action(*this, completion.action, ctx);

        set_state_direct(to);

        if(!skip_hooks)
        {
            if(auto it = table.find(to); it != table.end())
            {
                if(it->second.on_enter)
                {
                    it->second.on_enter(ctx, from, to, nullptr);
                }
            }
        }

        return output;
    }

    std::optional<Output_t> finalize_transition(std::optional<Output_t> result)
    {
        auto completion_out = process_completions();
        if(!result && completion_out)
        {
            result = std::move(completion_out);
        }
        drain_deferrals_for_current_state();
        return result;
    }

    std::optional<Output_t> process_completions()
    {
        if(!completion_limit_ || processing_completions_)
        {
            return std::nullopt;
        }
        assert(!async_inflight_);
        processing_completions_ = true;
        std::optional<Output_t> output;
        std::size_t steps = 0;
        try
        {
            while(const auto* completion = find_completion())
            {
                if(steps++ > completion_limit_)
                {
                    break;
                }
                auto result = apply_completion(*completion);
                if(result)
                {
                    output = std::move(result);
                }
            }
        } catch(...)
        {
            processing_completions_ = false;
            throw;
        }
        processing_completions_ = false;
        return output;
    }

    void drain_deferrals_for_current_state()
    {
        if(!deferral_enabled_ || draining_deferrals_) return;
        draining_deferrals_ = true;
        try
        {
            for(;;)
            {
                auto it = deferrals_.find(current_);
                if(it == deferrals_.end() || it->second.empty()) break;
                Input_t next = std::move(it->second.front());
                it->second.pop_front();
                handle_input(next);
            }
        } catch(...)
        {
            draining_deferrals_ = false;
            throw;
        }
        draining_deferrals_ = false;
    }

private:
    State_t current_{};
    std::unordered_map<State_t, StateHandlers> handlers_;
    std::unordered_map<State_t, std::vector<Transition>> transitions_;
    std::vector<Transition> any_transitions_;
    std::unordered_map<State_t, std::vector<Completion>> completion_transitions_;
    std::deque<Input_t> pending_inputs_;
    Ctx_t ctx_;
    Callable<void(Ctx_t&, const State_t&, const Input_t&)> machine_unhandled_{};
    Publisher_t publisher_{};
    std::unordered_map<State_t, std::deque<Input_t>> deferrals_;
    bool deferral_enabled_ = false;
    bool draining_deferrals_ = false;
    std::size_t completion_limit_ = 0;
    bool processing_completions_ = false;
    bool async_inflight_ = false;
};

} // namespace lsm

#endif
