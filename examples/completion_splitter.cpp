#include <iostream>
#include <optional>
#include <string>
#include <variant>

#include <lsm/core.hpp>

enum class State { Start, Setup, PathA, PathB };
struct Begin {};
using Input  = std::variant<Begin>;
using Output = std::string;

struct Context {
    bool choose_a = true;
};

using Machine = lsm::Machine<State, Input, Output, Context>;

static Machine::Builder make_builder() {
    Machine::Builder builder;
    builder.set_initial(State::Start);
    builder.from(State::Start)
        .on<Begin>()
        .to(State::Setup);
    builder.completion(State::Setup)
        .guard([](const Context& ctx) { return ctx.choose_a; })
        .action([](Context&) -> std::optional<Output> { return std::optional<Output>{"route to A"}; })
        .to(State::PathA);
    builder.completion(State::Setup)
        .guard([](const Context& ctx) { return !ctx.choose_a; })
        .action([](Context&) -> std::optional<Output> { return std::optional<Output>{"route to B"}; })
        .to(State::PathB);
    return builder;
}

int main() {
    auto run = [](bool choose_a) {
        Machine machine = make_builder().build({choose_a});
        auto out = machine.dispatch(Input{Begin{}});
        if (out) std::cout << *out << "\n";
        std::cout << "choose_a=" << choose_a << " -> state="
                  << (machine.state() == State::PathA ? "PathA" : "PathB") << "\n";
    };

    run(true);
    run(false);

    return 0;
}
