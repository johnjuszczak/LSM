#include <cassert>
#include <optional>
#include <variant>

#include <lsm/core.hpp>

enum class State { Idle, Done };
struct Go {};
using Input = std::variant<Go>;
using Output = int;

using Machine = lsm::Machine<State,
                             Input,
                             Output,
                             int,
                             lsm::policy::copy,
                             lsm::policy::Publisher<lsm::publisher::NullPublisher>>;

int main()
{
    Machine::Builder builder;
    builder.set_initial(State::Idle);
    builder.on<Go>(State::Idle, State::Done,
                   [](const Go&, int& ctx, lsm::publisher::NullPublisher&) {
                       ctx = 42;
                   });

    Machine machine = std::move(builder).build(0);

    {
        Machine::Effect::CompletionAction empty{};
        auto result = Machine::Effect::invoke_completion_action(machine, empty, machine.context());
        assert(!result);
    }

    {
        auto action = Machine::Effect::bind_completion_action(
            [](int&, lsm::publisher::NullPublisher& pub) { pub.publish(7); });
        auto result = Machine::Effect::invoke_completion_action(machine, action, machine.context());
        assert(!result);
    }

    {
        Machine::Effect::template TypedAction<Go> empty_typed{};
        auto lifted = Machine::Effect::template lift_variant_action<Go>(std::move(empty_typed));
        assert(!lifted);
    }

    auto out = machine.dispatch(Input{Go{}});
    assert(!out);
    assert(machine.state() == State::Done);
    assert(machine.context() == 42);

    return 0;
}
