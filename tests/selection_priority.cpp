#include <cassert>
#include <optional>
#include <string>
#include <variant>

#include <lsm/core.hpp>

enum class S { Start, Mid, High, Low, Any }; 
struct Alpha {};
struct Beta {};
struct Other {};
struct Reset {};

using Input = std::variant<Alpha, Beta, Other, Reset>;
using Output = std::string;
using Machine = lsm::Machine<S, Input, Output, std::monostate>;

int main() {
    Machine::Builder builder;
    builder.set_initial(S::Start);

    builder.on<Alpha>(S::Start, S::Low,
                      lsm::create_action<Output, Input, std::monostate>("low"));
    builder.on<Alpha>(S::Start, S::High,
                      lsm::create_action<Output, Input, std::monostate>("high"), nullptr, 5);

    builder.on<Reset>(S::High, S::Mid,
                      lsm::create_action<Output, Input, std::monostate>("reset"));

    builder.on<Beta>(S::Mid, S::High,
                     lsm::create_action<Output, Input, std::monostate>("first"));
    builder.on<Beta>(S::Mid, S::Low,
                     lsm::create_action<Output, Input, std::monostate>("second"));

    builder.any()
        .on<Other>()
        .to(S::Any);

    Machine machine = std::move(builder).build({});

    auto high = machine.dispatch(Input{Alpha{}});
    assert(high && *high == "high");
    assert(machine.state() == S::High);

    auto reset = machine.dispatch(Input{Reset{}});
    assert(reset && *reset == "reset");
    assert(machine.state() == S::Mid);

    auto beta = machine.dispatch(Input{Beta{}});
    assert(beta && *beta == "first");
    assert(machine.state() == S::High);

    auto other = machine.dispatch(Input{Other{}});
    assert(!other);
    assert(machine.state() == S::Any);

    return 0;
}

