#include <cassert>
#include <optional>
#include <string>
#include <variant>

#include <lsm/core.hpp>

enum class State { Idle, BranchA, BranchB, Finish };
struct Token {
    friend constexpr bool operator==(const Token&, const Token&) noexcept = default;
};
struct Switch {
    friend constexpr bool operator==(const Switch&, const Switch&) noexcept = default;
};

using Input = std::variant<Token, Switch>;
using Output = std::string;
using Machine = lsm::Machine<State, Input, Output, int>;

int main() {
    Machine::Builder builder;
    builder.set_initial(State::Idle);

    builder.on_value(State::Idle, State::BranchA, Input{Token{}},
                     [](const Input&, int& ctx) -> std::optional<Output> {
                         ctx = 1;
                         return std::optional<Output>{"first"};
                     },
                     [](const Input&, const int&) { return true; },
                     2);

    builder.on_value(State::Idle, State::BranchB, Input{Token{}},
                     [](const Input&, int& ctx) -> std::optional<Output> {
                         ctx = 2;
                         return std::optional<Output>{"second"};
                     },
                     [](const Input&, const int&) { return true; },
                     5);

    builder.completion(State::BranchB)
        .priority(1)
        .action([](int& ctx) -> std::optional<Output> {
            ctx += 10;
            return std::optional<Output>{"B-complete"};
        })
        .to(State::Finish);

    builder.completion(State::BranchB)
        .priority(0)
        .action([](int& ctx) -> std::optional<Output> {
            ctx += 100;
            return std::optional<Output>{"B-late"};
        })
        .to(State::BranchA);

    builder.completion(State::BranchA)
        .priority(3)
        .action([](int& ctx) -> std::optional<Output> {
            ctx = 200;
            return std::optional<Output>{"A-top"};
        })
        .to(State::Finish);

    builder.completion(State::BranchA)
        .priority(1)
        .action([](int& ctx) -> std::optional<Output> {
            ctx = -50;
            return std::optional<Output>{"A-low"};
        })
        .to(State::Idle);

    Machine machine = std::move(builder).build(0);

    auto token = machine.dispatch(Input{Token{}});
    assert(token && *token == "second");
    assert(machine.state() == State::Finish);
    assert(machine.context() == 12); // 2 from action + 10 from first completion

    return 0;
}
