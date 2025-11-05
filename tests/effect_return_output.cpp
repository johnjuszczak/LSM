#include <cassert>
#include <optional>
#include <variant>
#include <string>

#include <lsm/core.hpp>

enum class State { Idle, Completed };
struct Start {};
using Input = std::variant<Start>;
using Output = std::string;

using Machine = lsm::Machine<State, Input, Output, int>;

int main()
{
    Machine::Builder builder;
    builder.set_initial(State::Idle);
    builder.on<Start>(State::Idle, State::Completed);
    builder.completion(State::Completed)
        .to(State::Idle);

    Machine machine = std::move(builder).build(0);

    Machine::Effect::Action empty_transition{};
    Input sample{Start{}};
    auto manual_transition = Machine::Effect::invoke_transition_action(machine, empty_transition, sample, machine.context());
    assert(!manual_transition);

    Machine::Effect::CompletionAction empty_completion{};
    auto manual_completion = Machine::Effect::invoke_completion_action(machine, empty_completion, machine.context());
    assert(!manual_completion);

    Machine::Effect::template TypedAction<Start> empty_typed{};
    auto lifted = Machine::Effect::template lift_variant_action<Start>(std::move(empty_typed));
    assert(!lifted);

    auto non_empty_completion = Machine::Effect::bind_completion_action(
        [](int& ctx) -> std::optional<Output> {
            ctx = 1;
            return std::optional<Output>{"result"};
        });
    auto completion_value = Machine::Effect::invoke_completion_action(machine, non_empty_completion, machine.context());
    assert(completion_value && *completion_value == "result");

    machine.context() = 0;
    auto result = machine.dispatch(Input{Start{}});
    assert(!result);
    assert(machine.state() == State::Idle);

    auto update_out = machine.update();
    assert(!update_out);

    return 0;
}
