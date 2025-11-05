#include <cassert>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <lsm/core.hpp>

enum class S { Idle, Active };
struct Go {};
struct Loop {};

struct Context {
    std::vector<std::string> log;
};

using Input = std::variant<Go, Loop>;
using Output = std::string;
using Machine = lsm::Machine<S, Input, Output, Context>;

static std::string state_name(S s) {
    return s == S::Idle ? "Idle" : "Active";
}

int main() {
    Machine::Builder builder;
    builder.set_initial(S::Idle);
    builder.on_enter(S::Idle, [](Context& ctx, const S&, const S&, const Input*) {
        ctx.log.push_back("enter:Idle");
    });
    builder.on_exit(S::Idle, [](Context& ctx, const S&, const S& to, const Input*) {
        ctx.log.push_back("exit:Idle->" + state_name(to));
    });
    builder.on_enter(S::Active, [](Context& ctx, const S&, const S&, const Input*) {
        ctx.log.push_back("enter:Active");
    });
    builder.on_exit(S::Active, [](Context& ctx, const S&, const S& to, const Input*) {
        ctx.log.push_back("exit:Active->" + state_name(to));
    });

    builder.on<Go>(S::Idle, S::Active, lsm::create_action<Input, Context>());
    builder.on<Loop>(S::Active, S::Active, lsm::create_action<Input, Context>(), nullptr, 0, true);

    Machine machine = std::move(builder).build({});
    assert(machine.context().log.size() == 1);
    assert(machine.context().log[0] == "enter:Idle");

    machine.dispatch(Input{Go{}});
    const auto& log = machine.context().log;
    assert(log.size() == 3);
    assert(log[1] == "exit:Idle->Active");
    assert(log[2] == "enter:Active");

    machine.dispatch(Input{Loop{}});
    assert(machine.context().log.size() == 3);

    return 0;
}

