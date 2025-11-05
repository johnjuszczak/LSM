#include <cassert>
#include <optional>
#include <stdexcept>
#include <variant>

#include <lsm/core.hpp>

enum class State { Idle, Working };

struct Start {};
struct Reset {};
struct Unknown {};

using Input = std::variant<Start, Reset, Unknown>;
using Output = std::optional<int>;

struct Context
{
    bool allow = true;
    int handled = 0;
};

using Machine = lsm::Machine<State, Input, Output, Context>;

int main()
{
    Machine::Builder builder;
    builder.set_initial(State::Idle);
    builder.enable_deferral(true);

    builder.on<Start>(State::Idle, State::Working,
                      [](const Start&, Context& ctx) -> Output {
                          ctx.handled += 1;
                          return std::nullopt;
                      },
                      [](const Input&, const Context& ctx) { return ctx.allow; },
                      0,
                      false,
                      true);

    builder.on<Start>(State::Working, State::Working,
                      [](const Start&, Context& ctx) -> Output {
                          ctx.handled += 1;
                          return std::nullopt;
                      });

    builder.on<Reset>(State::Working, State::Idle,
                      [](const Reset&, Context&) -> Output { return std::nullopt; });

    builder.on_unhandled(State::Working,
                         [](Context& ctx, const State&, const Input&) {
                             ctx.handled += 10;
                         });

    int machine_unhandled_calls = 0;
    builder.on_unhandled([&machine_unhandled_calls](Context&, const State&, const Input&) {
        machine_unhandled_calls += 1;
        throw std::runtime_error("swallow");
    });

    Machine machine = std::move(builder).build(Context{true, 0});

    machine.dispatch(Input{Start{}});
    assert(machine.state() == State::Working);
    assert(machine.context().handled >= 1);

    machine.dispatch(Input{Unknown{}});
    assert(machine.context().handled >= 11);
    assert(machine.state() == State::Working);

    machine.dispatch(Input{Reset{}});
    assert(machine.state() == State::Idle);

    machine.context().allow = false;
    bool swallowed = false;
    try
    {
        machine.dispatch(Input{Start{}});
    }
    catch(...)
    {
        swallowed = true;
    }
    assert(!swallowed);
    assert(machine_unhandled_calls == 1);
    assert(machine.state() == State::Idle);

    return 0;
}
