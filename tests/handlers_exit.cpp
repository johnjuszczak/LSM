#include <cassert>
#include <optional>
#include <variant>

#include <lsm/core.hpp>

enum class State { Idle, Active };
struct Activate {};
using Input = std::variant<Activate>;
using Output = std::optional<int>;

struct ExitHandler
{
    int* tracker{};

    void on_exit(int& ctx, const State&, const State& next, const Input*)
    {
        *tracker = ctx = static_cast<int>(next);
    }
};

using Machine = lsm::Machine<State, Input, Output, int>;

int main()
{
    int observed = -1;

    Machine::Builder builder;
    builder.set_initial(State::Idle);
    ExitHandler handler{&observed};
    builder.on_state(State::Idle, handler, lsm::bind::by_ref{});
    builder.on<Activate>(State::Idle, State::Active);

    Machine machine = std::move(builder).build(5);

    auto out = machine.dispatch(Input{Activate{}});
    assert(!out);
    assert(machine.state() == State::Active);
    assert(observed == static_cast<int>(State::Active));
    assert(machine.context() == static_cast<int>(State::Active));

    return 0;
}
