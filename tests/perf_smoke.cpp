#include <cstdlib>
#include <variant>

#include <lsm/core.hpp>

enum class S { Idle, Active };
struct Ping {};
using Input = std::variant<Ping>;
using Machine = lsm::Machine<S, Input, std::monostate, std::monostate>;

int main() {
    Machine::Builder builder;
    builder.set_initial(S::Idle);
    builder.on<Ping>(S::Idle, S::Active, lsm::create_action<Input, std::monostate>());
    builder.on<Ping>(S::Active, S::Idle, lsm::create_action<Input, std::monostate>());

    auto machine = std::move(builder).build({});

    const char* gate = std::getenv("LSM_ENABLE_BENCH_SMOKE");
    if (gate) {
        for (int i = 0; i < 50000; ++i) {
            machine.dispatch(Input{Ping{}});
            machine.dispatch(Input{Ping{}});
        }
    } else {
        machine.dispatch(Input{Ping{}});
        machine.dispatch(Input{Ping{}});
    }

    return 0;
}

