#include <cassert>
#include <optional>
#include <string>
#include <variant>

#include <lsm/core.hpp>

enum class State { Start, ViaPriority, ViaAny, ResetDone };

struct Alpha {
    friend bool operator==(const Alpha&, const Alpha&) noexcept { return true; }
};
struct Beta {
    friend bool operator==(const Beta&, const Beta&) noexcept { return true; }
};
struct Reset {
    friend bool operator==(const Reset&, const Reset&) noexcept { return true; }
};

using Input = std::variant<Alpha, Beta, Reset>;
using Output = std::string;
using Machine = lsm::Machine<State, Input, Output, std::monostate>;

int main() {
    {
        Alpha a{};
        Beta b{};
        Reset r{};
        assert(a == a);
        assert(b == b);
        assert(r == r);
    }

    Machine::Builder builder;
    builder.set_initial(State::Start);

    builder.from(State::Start)
        .on<Alpha>()
        .action([](const Alpha&, std::monostate&) -> std::optional<Output> {
            return std::optional<Output>{"low"};
        })
        .priority(1)
        .to(State::ViaAny);

    builder.from(State::Start)
        .on<Alpha>()
        .action([](const Alpha&, std::monostate&) -> std::optional<Output> {
            return std::optional<Output>{"high"};
        })
        .priority(9)
        .to(State::ViaPriority);

    builder.any()
        .on<Beta>()
        .action([](const Beta&, std::monostate&) -> std::optional<Output> {
            return std::optional<Output>{"beta-any"};
        })
        .priority(3)
        .to(State::ViaAny);

    builder.any()
        .on_value(Input{Reset{}})
        .action([](const Input&, std::monostate&) -> std::optional<Output> {
            return std::optional<Output>{"reset-any"};
        })
        .priority(7)
        .to(State::ResetDone);

    Machine machine = std::move(builder).build({});

    auto alpha = machine.dispatch(Input{Alpha{}});
    assert(alpha && *alpha == "high");
    assert(machine.state() == State::ViaPriority);

    auto beta = machine.dispatch(Input{Beta{}});
    assert(beta && *beta == "beta-any");
    assert(machine.state() == State::ViaAny);

    auto reset = machine.dispatch(Input{Reset{}});
    assert(reset && *reset == "reset-any");
    assert(machine.state() == State::ResetDone);

    return 0;
}
