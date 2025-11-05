#pragma once

#include <utility>

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
using Machine = MachineImpl<State, Input, Output, Context, CallablePolicy, EffectPolicy>;

} // namespace lsm
