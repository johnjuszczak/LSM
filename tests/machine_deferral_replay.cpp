#include <cassert>
#include <optional>
#include <stdexcept>
#include <variant>
#include <vector>

#include <lsm/core.hpp>

enum class State { Idle, Working };

struct Start
{
    int id{};
};
struct Tick
{
};
struct Unknown
{
    int code{};
};

using Input = std::variant<Start, Tick, Unknown>;
using Output = int;

struct Context
{
    std::vector<int> replayed;
    std::vector<int> outputs;
    int idle_unhandled = 0;
    int machine_unhandled = 0;
};

using Machine = lsm::Machine<State, Input, Output, Context>;

int main()
{
    Machine::Builder builder;
    builder.set_initial(State::Idle);
    builder.enable_deferral(true);

    builder.on<Start>(State::Idle, State::Working,
                      [](const Start&, Context&) -> std::optional<Output> {
                          return std::nullopt;
                      },
                      nullptr,
                      0,
                      false,
                      true);

    builder.on<Start>(State::Working, State::Working,
                      [](const Start& start, Context& ctx) -> std::optional<Output> {
                          ctx.replayed.push_back(start.id);
                          ctx.outputs.push_back(start.id);
                          return Output{start.id};
                      },
                      nullptr,
                      0,
                      true,
                      false);

    builder.on<Tick>(State::Working, State::Idle,
                     [](const Tick&, Context& ctx) -> std::optional<Output> {
                         ctx.replayed.push_back(99);
                         ctx.outputs.push_back(99);
                         return Output{99};
                     });

    builder.on_unhandled(State::Idle,
                         [](Context& ctx, const State&, const Input&) {
                             ctx.idle_unhandled += 1;
                         });

    builder.on_unhandled([](Context& ctx, const State&, const Input&) {
        ctx.machine_unhandled += 1;
        throw std::runtime_error{"machine-level"};
    });

    Machine machine = std::move(builder).build({});

    machine.enqueue(Input{Unknown{7}});
    machine.enqueue(Input{Start{3}});
    machine.enqueue(Input{Unknown{9}});
    machine.enqueue(Input{Tick{}});

    auto outputs = machine.dispatch_all();
    assert(outputs.size() == 1);
    assert(outputs[0] == 99);

    const auto& ctx = machine.context();
    assert(ctx.replayed.size() == 2);
    assert(ctx.replayed[0] == 3);
    assert(ctx.replayed[1] == 99);
    assert(ctx.outputs.size() == 2);
    assert(ctx.outputs[0] == 3);
    assert(ctx.outputs[1] == 99);
    assert(ctx.idle_unhandled == 1);
    assert(ctx.machine_unhandled == 1);
    assert(machine.state() == State::Idle);

    return 0;
}
