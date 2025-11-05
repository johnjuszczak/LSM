#include <cassert>
#include <memory>
#include <optional>
#include <variant>

#include <lsm/core.hpp>

enum class State { Idle, Pointer, Shared };
struct StepPointer {};
struct StepShared {};
struct Reset {};

using Input = std::variant<StepPointer, StepShared, Reset>;
using Output = std::optional<int>;

struct Handler
{
    int* counter{};

    void on_enter(int&, const State&, const State&, const Input*)
    {
        (*counter) += 1;
    }

    void on_exit(int&, const State&, const State&, const Input*)
    {
        (*counter) += 10;
    }

    std::optional<int> on_do(int&, const State&)
    {
        (*counter) += 100;
        return std::nullopt;
    }
};

using Machine = lsm::Machine<State, Input, Output, int>;

int main()
{
    int ref_counter = 0;
    int ptr_counter = 0;
    int shared_counter = 0;

    Machine::Builder builder;
    builder.set_initial(State::Idle);

    Handler ref_handler{&ref_counter};
    builder.on_state(State::Idle, ref_handler);

    Handler ptr_handler{&ptr_counter};
    builder.on_state(State::Pointer, &ptr_handler, lsm::bind::by_ptr{});

    auto shared_handler = std::make_shared<Handler>(&shared_counter);
    builder.on_state(State::Shared, shared_handler, lsm::bind::by_shared{});

    builder.on<StepPointer>(State::Idle, State::Pointer,
                            [](const StepPointer&, int&) -> Output { return std::nullopt; });

    builder.on<StepShared>(State::Pointer, State::Shared,
                           [](const StepShared&, int&) -> Output { return std::nullopt; });

    builder.on<Reset>(State::Shared, State::Idle,
                      [](const Reset&, int&) -> Output { return std::nullopt; });

    Machine machine = std::move(builder).build(0);

    auto run = [&machine]() {
        while(!machine.update())
        {
            if(auto res = machine.update())
            {
                (void)res;
                break;
            }
            break;
        }
    };

    machine.dispatch(Input{StepPointer{}});
    machine.update();

    machine.dispatch(Input{StepShared{}});
    machine.update();

    machine.dispatch(Input{Reset{}});
    machine.update();

    assert(machine.state() == State::Idle);
    assert(ref_counter > 0);
    assert(ptr_counter > 0);
    assert(shared_counter > 0);

    (void)run;
    return 0;
}
