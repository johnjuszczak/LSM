#include <cassert>
#include <optional>
#include <string>
#include <vector>
#include <variant>

#include <lsm/core.hpp>

enum class S { Idle, Stage, Active };
struct Job { int id; };
struct Reset {};

using Input = std::variant<Job, Reset>;
using Output = std::string;

struct Ctx {
    std::vector<int> order;
};

using M = lsm::Machine<S, Input, Output, Ctx>;

int main() {
    M::Builder builder;
    builder.set_initial(S::Idle);
    builder.enable_deferral(true);

    builder.on<Job>(S::Idle, S::Stage,
        lsm::create_action<Input, Ctx>(), nullptr, 0, false, true);

    builder.on<Job>(S::Stage, S::Active,
        [](const Job& job, Ctx& ctx) -> std::optional<Output> {
            ctx.order.push_back(job.id);
            return std::optional<Output>{};
        });

    builder.on<Job>(S::Active, S::Stage,
        lsm::create_action<Input, Ctx>(), nullptr, 0, false, true);

    builder.on<Reset>(S::Active, S::Idle,
        lsm::create_action<Input, Ctx>());

    auto machine = std::move(builder).build({});

    auto first = machine.dispatch(Input{Job{1}});
    assert(!first);
    assert(machine.state() == S::Active);
    assert(machine.context().order.size() == 1 && machine.context().order[0] == 1);

    auto second = machine.dispatch(Input{Job{2}});
    assert(!second);
    assert(machine.context().order.size() == 2 && machine.context().order[1] == 2);
    assert(machine.state() == S::Active);

    machine.dispatch(Input{Reset{}});
    assert(machine.state() == S::Idle);

    auto third = machine.dispatch(Input{Job{3}});
    assert(!third);
    assert(machine.context().order.size() == 3 && machine.context().order[2] == 3);

    return 0;
}
