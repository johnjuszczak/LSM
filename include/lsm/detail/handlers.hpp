#ifndef LSM_DETAIL_HANDLERS_HPP
#define LSM_DETAIL_HANDLERS_HPP

#include <memory>
#include <type_traits>
#include <utility>

#include <lsm/detail/concepts.hpp>

namespace lsm
{
namespace detail
{

template <class T>
inline constexpr bool dependent_false_v = false;

// Binding tags for handler objects
struct by_ref
{
};
struct by_ptr
{
};
struct by_shared
{
};

template <typename State, typename Input, typename Output, typename Context, PolicyHasCallableTemplate CallablePolicy, typename Effect>
struct StateHandlers
{
    using State_t = State;
    using Input_t = Input;
    using Output_t = Output;
    using Ctx_t = Context;
    using Effect_t = Effect;

    using EnterExitSig = void(Ctx_t&, const State_t&, const State_t&, const Input_t*);
    using UnhandledSig = void(Ctx_t&, const State_t&, const Input_t&);

    template <typename Sig>
    using Callable = typename CallablePolicy::template Callable<Sig>;

    Callable<EnterExitSig> on_enter;
    Callable<EnterExitSig> on_exit;
    typename Effect::StateAction on_do;
    Callable<UnhandledSig> on_unhandled;
};

// Helper adaptors that turn handler objects or pointers into stored callables
template <class SH, class Handler>
auto bind_on_enter([[maybe_unused]] Handler& h, by_ref) -> typename SH::template Callable<typename SH::EnterExitSig>
{
    using T = std::remove_reference_t<Handler>;
    if constexpr(has_on_enter<T, typename SH::State_t, typename SH::Input_t, typename SH::Output_t, typename SH::Ctx_t>)
    {
        return typename SH::template Callable<typename SH::EnterExitSig>{
            [ptr = std::addressof(h)](typename SH::Ctx_t& ctx, const typename SH::State_t& from,
                                      const typename SH::State_t& to, const typename SH::Input_t* in) {
                ptr->on_enter(ctx, from, to, in);
            }};
    }
    else
    {
        return typename SH::template Callable<typename SH::EnterExitSig>{};
    }
}

template <class SH, class Handler>
auto bind_on_enter([[maybe_unused]] Handler* h, by_ptr) -> typename SH::template Callable<typename SH::EnterExitSig>
{
    using T = std::remove_pointer_t<Handler*>;
    (void)sizeof(T); // silence unused in case
    if constexpr(has_on_enter<std::remove_pointer_t<Handler>, typename SH::State_t, typename SH::Input_t, typename SH::Output_t, typename SH::Ctx_t>)
    {
        return typename SH::template Callable<typename SH::EnterExitSig>{
            [h](typename SH::Ctx_t& ctx, const typename SH::State_t& from,
                const typename SH::State_t& to, const typename SH::Input_t* in) {
                h->on_enter(ctx, from, to, in);
            }};
    }
    else
    {
        return typename SH::template Callable<typename SH::EnterExitSig>{};
    }
}

template <class SH, class Handler>
auto bind_on_enter([[maybe_unused]] std::shared_ptr<Handler> h, by_shared) -> typename SH::template Callable<typename SH::EnterExitSig>
{
    if constexpr(has_on_enter<Handler, typename SH::State_t, typename SH::Input_t, typename SH::Output_t, typename SH::Ctx_t>)
    {
        return typename SH::template Callable<typename SH::EnterExitSig>{
            [h = std::move(h)](typename SH::Ctx_t& ctx, const typename SH::State_t& from,
                                const typename SH::State_t& to, const typename SH::Input_t* in) {
                h->on_enter(ctx, from, to, in);
            }};
    }
    else
    {
        return typename SH::template Callable<typename SH::EnterExitSig>{};
    }
}

template <class SH, class Handler>
auto bind_on_exit([[maybe_unused]] Handler& h, by_ref) -> typename SH::template Callable<typename SH::EnterExitSig>
{
    using T = std::remove_reference_t<Handler>;
    if constexpr(has_on_exit<T, typename SH::State_t, typename SH::Input_t, typename SH::Output_t, typename SH::Ctx_t>)
    {
        return typename SH::template Callable<typename SH::EnterExitSig>{
            [ptr = std::addressof(h)](typename SH::Ctx_t& ctx, const typename SH::State_t& from,
                                      const typename SH::State_t& to, const typename SH::Input_t* in) {
                ptr->on_exit(ctx, from, to, in);
            }};
    }
    else
    {
        return typename SH::template Callable<typename SH::EnterExitSig>{};
    }
}

template <class SH, class Handler>
auto bind_on_exit([[maybe_unused]] Handler* h, by_ptr) -> typename SH::template Callable<typename SH::EnterExitSig>
{
    if constexpr(has_on_exit<std::remove_pointer_t<Handler>, typename SH::State_t, typename SH::Input_t, typename SH::Output_t, typename SH::Ctx_t>)
    {
        return typename SH::template Callable<typename SH::EnterExitSig>{
            [h](typename SH::Ctx_t& ctx, const typename SH::State_t& from,
                const typename SH::State_t& to, const typename SH::Input_t* in) {
                h->on_exit(ctx, from, to, in);
            }};
    }
    else
    {
        return typename SH::template Callable<typename SH::EnterExitSig>{};
    }
}

template <class SH, class Handler>
auto bind_on_exit([[maybe_unused]] std::shared_ptr<Handler> h, by_shared) -> typename SH::template Callable<typename SH::EnterExitSig>
{
    if constexpr(has_on_exit<Handler, typename SH::State_t, typename SH::Input_t, typename SH::Output_t, typename SH::Ctx_t>)
    {
        return typename SH::template Callable<typename SH::EnterExitSig>{
            [h = std::move(h)](typename SH::Ctx_t& ctx, const typename SH::State_t& from,
                                const typename SH::State_t& to, const typename SH::Input_t* in) {
                h->on_exit(ctx, from, to, in);
            }};
    }
    else
    {
        return typename SH::template Callable<typename SH::EnterExitSig>{};
    }
}

template <class SH, class Handler>
auto bind_on_do([[maybe_unused]] Handler& h, by_ref) -> typename SH::Effect_t::StateAction
{
    using T = std::remove_reference_t<Handler>;
    using Effect = typename SH::Effect_t;
    using State = typename SH::State_t;
    using Ctx = typename SH::Ctx_t;
    using Publisher = typename Effect::Publisher;

    if constexpr(has_on_do_return<T, State, typename SH::Output_t, Ctx>)
    {
        auto fn = [ptr = std::addressof(h)](Ctx& ctx, const State& st) {
            return ptr->on_do(ctx, st);
        };
        return Effect::bind_state_action(std::move(fn));
    }
    else if constexpr(has_on_do_publish<T, State, Publisher, Ctx>)
    {
        auto fn = [ptr = std::addressof(h)](Ctx& ctx, const State& st, Publisher& pub) {
            ptr->on_do(ctx, st, pub);
        };
        return Effect::bind_state_action(std::move(fn));
    }
    else
    {
        if constexpr(has_on_do_member<T>)
        {
            static_assert(dependent_false_v<T>, "Handler::on_do signature must match active policy");
        }
        return typename Effect::StateAction{};
    }
}

template <class SH, class Handler>
auto bind_on_do([[maybe_unused]] Handler* h, by_ptr) -> typename SH::Effect_t::StateAction
{
    using T = std::remove_pointer_t<Handler>;
    using Effect = typename SH::Effect_t;
    using State = typename SH::State_t;
    using Ctx = typename SH::Ctx_t;
    using Publisher = typename Effect::Publisher;

    if constexpr(has_on_do_return<T, State, typename SH::Output_t, Ctx>)
    {
        auto fn = [h](Ctx& ctx, const State& st) { return h->on_do(ctx, st); };
        return Effect::bind_state_action(std::move(fn));
    }
    else if constexpr(has_on_do_publish<T, State, Publisher, Ctx>)
    {
        auto fn = [h](Ctx& ctx, const State& st, Publisher& pub) { h->on_do(ctx, st, pub); };
        return Effect::bind_state_action(std::move(fn));
    }
    else
    {
        if constexpr(has_on_do_member<T>)
        {
            static_assert(dependent_false_v<T>, "Handler::on_do signature must match active policy");
        }
        return typename Effect::StateAction{};
    }
}

template <class SH, class Handler>
auto bind_on_do([[maybe_unused]] std::shared_ptr<Handler> h, by_shared) -> typename SH::Effect_t::StateAction
{
    using Effect = typename SH::Effect_t;
    using State = typename SH::State_t;
    using Ctx = typename SH::Ctx_t;
    using Publisher = typename Effect::Publisher;

    if constexpr(has_on_do_return<Handler, State, typename SH::Output_t, Ctx>)
    {
        auto fn = [h = std::move(h)](Ctx& ctx, const State& st) {
            return h->on_do(ctx, st);
        };
        return Effect::bind_state_action(std::move(fn));
    }
    else if constexpr(has_on_do_publish<Handler, State, Publisher, Ctx>)
    {
        auto fn = [h = std::move(h)](Ctx& ctx, const State& st, Publisher& pub) {
            h->on_do(ctx, st, pub);
        };
        return Effect::bind_state_action(std::move(fn));
    }
    else
    {
        if constexpr(has_on_do_member<Handler>)
        {
            static_assert(dependent_false_v<Handler>, "Handler::on_do signature must match active policy");
        }
        return typename Effect::StateAction{};
    }
}

} // namespace detail

namespace bind
{

using by_ref = detail::by_ref;
using by_ptr = detail::by_ptr;
using by_shared = detail::by_shared;

} // namespace bind

} // namespace lsm

#endif
