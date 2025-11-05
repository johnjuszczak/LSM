#include <lsm/core.hpp>

using namespace lsm;

enum class S { A };
using In = int;
using Out = int;
struct C {};

struct OnlyEnter
{
    void on_enter(C&, const S&, const S&, const In*) {}
};
struct OnlyExit
{
    void on_exit(C&, const S&, const S&, const In*) {}
};
struct OnlyDoReturn
{
    std::optional<Out> on_do(C&, const S&) { return std::nullopt; }
};
struct OnlyDoPublish
{
    void on_do(C&, const S&, lsm::publisher::NullPublisher&) {}
};

static_assert(has_on_enter<OnlyEnter, S, In, Out, C>);
static_assert(has_on_exit<OnlyExit, S, In, Out, C>);
static_assert(has_on_do_return<OnlyDoReturn, S, Out, C>);
static_assert(has_on_do_publish<OnlyDoPublish, S, lsm::publisher::NullPublisher, C>);
static_assert(!has_on_do_member<OnlyEnter>);
static_assert(has_on_do_member<OnlyDoReturn>);

static_assert(StateHandlerFor<OnlyEnter, S, In, Out, C, policy::ReturnOutput<Out>>);
static_assert(StateHandlerFor<OnlyExit, S, In, Out, C, policy::ReturnOutput<Out>>);
static_assert(StateHandlerFor<OnlyDoReturn, S, In, Out, C, policy::ReturnOutput<Out>>);
static_assert(StateHandlerFor<OnlyDoPublish, S, In, Out, C, policy::Publisher<lsm::publisher::NullPublisher>>);

int main()
{
    return 0;
}
