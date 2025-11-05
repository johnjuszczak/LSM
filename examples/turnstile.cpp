#include <iostream>
#include <optional>
#include <string>
#include <variant>

#include <lsm/core.hpp>

enum class State { Locked, Unlocked };
struct Coin {};
struct Push {};
using Input = std::variant<Coin, Push>;
using Output = std::string;

struct Context {
    int coins = 0;
};

using Machine = lsm::Machine<State, Input, Output, Context>;

int main() {
    Machine::Builder builder;
    builder.set_initial(State::Locked);
    builder.on_unhandled([](Context&, const State& state, const Input&) {
        std::cout << "unhandled in state=" << (state == State::Locked ? "Locked" : "Unlocked") << "\n";
    });

    builder.on<Coin>(State::Locked, State::Unlocked,
        [](const Coin&, Context& ctx) -> std::optional<Output> {
            ++ctx.coins;
            return std::optional<Output>{"coin accepted"};
        });

    builder.on<Push>(State::Locked, State::Locked,
        [](const Push&, Context&) -> std::optional<Output> {
            return std::optional<Output>{"locked"};
        }, nullptr, 1, true);

    builder.on<Push>(State::Unlocked, State::Locked,
        lsm::create_action<Output, Input, Context>("pass through"));

    builder.on<Coin>(State::Unlocked, State::Unlocked,
        lsm::create_action<Output, Input, Context>("already unlocked"), nullptr, 1, true);

    Machine machine = std::move(builder).build({});

    auto step = [&](const Input& in) {
        if (auto out = machine.dispatch(in)) {
            std::cout << *out << "\n";
        }
        std::cout << "state=" << (machine.state() == State::Locked ? "Locked" : "Unlocked")
                  << " coins=" << machine.context().coins << "\n";
    };

    step(Input{Push{}});
    step(Input{Coin{}});
    step(Input{Push{}});
    step(Input{Coin{}});
    step(Input{Coin{}});
    step(Input{Push{}});
    step(Input{Push{}});

    return 0;
}

