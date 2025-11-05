#include <cassert>
#include <optional>
#include <variant>
#include <vector>

#include <lsm/core.hpp>

enum class State { Idle, Active };
struct Start
{
    int value{};
};
struct Reset
{
};

using Input = std::variant<Start, Reset>;
using Output = int;

struct Context
{
    std::vector<State> visited;
};

using Machine = lsm::Machine<State, Input, Output, Context>;

int main()
{
    Machine::Builder builder;
    builder.set_initial(State::Idle);

    builder.on<Start>(State::Idle, State::Active,
                      [](const Start& start, Context& ctx) -> std::optional<Output> {
                          ctx.visited.push_back(State::Active);
                          return start.value;
                      });

    builder.on<Reset>(State::Active, State::Idle,
                      [](const Reset&, Context& ctx) -> std::optional<Output> {
                          ctx.visited.push_back(State::Idle);
                          return Output{-7};
                      });

    Machine machine = std::move(builder).build({});

    Input start_event = Input{Start{42}};
    machine.enqueue(start_event);         // copy overload
    machine.enqueue(Input{Reset{}});      // move overload

    auto outputs = machine.dispatch_all();
    assert(outputs.size() == 2);
    assert(outputs[0] == 42);
    assert(outputs[1] == -7);

    assert(machine.context().visited.size() == 2);
    assert(machine.context().visited[0] == State::Active);
    assert(machine.context().visited[1] == State::Idle);
    assert(machine.state() == State::Idle);

    auto drained = machine.dispatch_all();
    assert(drained.empty());

    return 0;
}
