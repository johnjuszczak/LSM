#ifndef LSM_DETAIL_HELPERS_HPP
#define LSM_DETAIL_HELPERS_HPP

#include <optional>
#include <utility>

namespace lsm
{

template <class Output, class Input, class Ctx>
auto create_action(Output value)
{
    return [v = std::move(value)](const Input&, Ctx&) -> std::optional<Output> { return v; };
}

template <class Input, class Ctx>
auto create_action()
{
    return [](const Input&, Ctx&) -> std::nullopt_t { return std::nullopt; };
}

} // namespace lsm

#endif
