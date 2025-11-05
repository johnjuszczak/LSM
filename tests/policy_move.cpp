#include <cassert>
#include <coroutine>
#include <memory>
#include <optional>
#include <variant>

#include <lsm/core.hpp>
#include <lsm/cosm.hpp>

enum class S { Idle, Next };
struct Kick {};
using Input = std::variant<Kick>;
using Output = int;
struct Ctx { int value = 0; };

using MoveMachine = lsm::Machine<S, Input, Output, Ctx, lsm::policy::move>;

int main() {
    MoveMachine::Builder builder;
    builder.set_initial(S::Idle);

    MoveMachine::Transition transition;
    transition.from = S::Idle;
    transition.to = S::Next;
    transition.guard = MoveMachine::Guard{ [token = std::make_unique<int>(1)](const Input&, const Ctx&) { return *token == 1; } };
    transition.action = MoveMachine::Action{ [payload = std::make_unique<int>(7)](const Input&, Ctx& ctx) -> std::optional<Output> {
        ctx.value = *payload;
        return std::optional<Output>{ctx.value};
    } };
    builder.add_transition(std::move(transition));

    auto machine = std::move(builder).build({});
    auto out = machine.dispatch(Input{Kick{}});
    assert(out && *out == 7);
    assert(machine.context().value == 7);
    assert(machine.state() == S::Next);

    using CoMachine = MoveMachine;
    typename CoMachine::Builder async_builder;
    async_builder.set_initial(S::Idle);
    async_builder.on<Kick>(S::Idle, S::Next,
                           [](const Kick&, Ctx&) -> std::optional<Output> { return std::nullopt; });
    auto async_machine = std::move(async_builder).build({});
    {
        typename CoMachine::Builder verify;
        verify.set_initial(S::Idle);
        verify.on<Kick>(S::Idle, S::Next,
                        [](const Kick&, Ctx&) -> std::optional<Output> { return std::nullopt; });
        auto check = std::move(verify).build({});
        auto direct = check.dispatch(Input{Kick{}});
        assert(!direct);
        assert(check.state() == S::Next);
    }
    lsm::co::Adapter<CoMachine> adapter(async_machine);
    typename CoMachine::template Callable<lsm::co::Task<std::optional<Output>>(const Input&, Ctx&, lsm::co::CancelToken, typename CoMachine::Publisher_t&)> async_action =
        [payload = std::make_unique<int>(9)](const Input&, Ctx& ctx, lsm::co::CancelToken, auto&) -> lsm::co::Task<std::optional<Output>> {
            ctx.value = *payload;
            co_return std::optional<Output>{ctx.value};
        };
    adapter.bind_async(S::Idle, S::Next, std::move(async_action));
    auto task = adapter.dispatch_async(Input{Kick{}});
    while (!task.await_ready()) {
        task.await_suspend(std::noop_coroutine());
    }
    auto async_out = task.await_resume();
    assert(async_out && *async_out == 9);
    int ctx_value = async_machine.context().value;
    assert(ctx_value == 9);
    assert(async_machine.state() == S::Next);
}
