#ifndef LSM_DETAIL_TYPES_HPP
#define LSM_DETAIL_TYPES_HPP

#include <optional>

#include <lsm/detail/concepts.hpp>

namespace lsm
{
namespace detail
{

template <typename State, typename Input, typename Output, typename Context, typename CallablePolicy, typename Effect>
struct Transition
{
    using State_t = State;
    using Input_t = Input;
    using Output_t = Output;
    using Ctx_t = Context;

    template <typename Sig>
    using Callable = typename CallablePolicy::template Callable<Sig>;

    using Guard = Callable<bool(const Input_t&, const Ctx_t&)>;
    using Action = typename Effect::Action;

    State_t from{};
    State_t to{};
    bool suppress_enter_exit = true;
    int priority = 0;
    bool defer = false;
    mutable Guard guard{};
    mutable Action action{};
};

template <typename State, typename Input, typename Output, typename Context, typename CallablePolicy, typename Effect>
struct CompletionTransition
{
    using State_t = State;
    using Input_t = Input;
    using Output_t = Output;
    using Ctx_t = Context;

    template <typename Sig>
    using Callable = typename CallablePolicy::template Callable<Sig>;

    using Guard = Callable<bool(const Ctx_t&)>;
    using Action = typename Effect::CompletionAction;

    State_t from{};
    State_t to{};
    bool suppress_enter_exit = true;
    int priority = 0;
    mutable Guard guard{};
    mutable Action action{};
};

struct AnyState_t
{
};

} // namespace detail
} // namespace lsm

#endif
