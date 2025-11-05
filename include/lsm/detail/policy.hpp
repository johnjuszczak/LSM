#ifndef LSM_DETAIL_POLICY_HPP
#define LSM_DETAIL_POLICY_HPP

#include <functional>

namespace lsm
{
namespace detail
{

struct policy_copy
{
    template <typename Sig>
    using Callable = std::function<Sig>;
};

struct policy_move
{
    template <typename Sig>
    using Callable = std::move_only_function<Sig>;
};

} // namespace detail

namespace policy
{

using copy = detail::policy_copy;
using move = detail::policy_move;

template <class Output>
struct ReturnOutput
{
};

template <class PublisherType>
struct Publisher
{
};

} // namespace policy
} // namespace lsm

#endif
