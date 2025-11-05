#include <cassert>
#include <optional>
#include <variant>

#include <lsm/core.hpp>

enum class State { Idle, Next };
struct Trigger {};

using Input = std::variant<Trigger>;
using Output = int;

struct Context
{
    bool allow = false;
};

using Machine = lsm::Machine<State, Input, Output, Context>;

int main()
{
    Machine::Builder builder;
    builder.set_initial(State::Idle);
    builder.on<Trigger>(State::Idle, State::Next,
                        [](const Input&, Context&) -> std::optional<Output> { return std::nullopt; },
                        [](const Input&, const Context& ctx) { return ctx.allow; });

    Machine machine = std::move(builder).build({false});

    auto blocked = machine.dispatch(Input{Trigger{}});
    assert(!blocked);
    assert(machine.state() == State::Idle);

    machine.context().allow = true;
    auto allowed = machine.dispatch(Input{Trigger{}});
    assert(!allowed);
    assert(machine.state() == State::Next);

    return 0;
}
