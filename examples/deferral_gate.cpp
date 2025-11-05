#include <iostream>
#include <optional>
#include <queue>
#include <string>
#include <variant>

#include <lsm/core.hpp>

enum class State { Waiting, Ready, Processing };
struct Enqueue { int id; };
struct Tick {};
using Input = std::variant<Enqueue, Tick>;
using Output = std::string;

struct Context {
    std::queue<int> completed;
};

using Machine = lsm::Machine<State, Input, Output, Context>;

int main() {
    Machine::Builder builder;
    builder.set_initial(State::Waiting);
    builder.enable_deferral(true);

    builder.on<Enqueue>(State::Waiting, State::Ready,
        [](const Enqueue& e, Context&) -> std::optional<Output> {
            return std::optional<Output>{"deferred " + std::to_string(e.id)};
        },
        nullptr, 0, false, true);

    builder.on<Enqueue>(State::Ready, State::Processing,
        [](const Enqueue& e, Context& ctx) -> std::optional<Output> {
            std::cout << "drain -> " << e.id << "\n";
            ctx.completed.push(e.id);
            return std::optional<Output>{"processing " + std::to_string(e.id)};
        });

    builder.on<Tick>(State::Processing, State::Waiting,
        lsm::create_action<Output, Input, Context>("done"));

    Machine machine = std::move(builder).build({});

    auto step = [&](const Input& in) {
        if (auto out = machine.dispatch(in)) {
            std::cout << *out << "\n";
        }
        std::cout << "state=" << static_cast<int>(machine.state()) << "\n";
    };

    step(Input{Enqueue{1}});
    step(Input{Tick{}});
    step(Input{Enqueue{2}});
    step(Input{Tick{}});

    while (!machine.context().completed.empty()) {
        std::cout << "completed: " << machine.context().completed.front() << "\n";
        machine.context().completed.pop();
    }

    return 0;
}
