#include <iostream>
#include <optional>
#include <string>
#include <variant>

#include <lsm/cosm.hpp>

enum class State { Idle, Active, Done };
struct Start {};
struct Stop {};

using Input = std::variant<Start, Stop>;
using Output = std::string;
struct Ctx {
    int steps = 0;
};

using Machine = lsm::CoMachine<State, Input, Output, Ctx>;

struct ManualEvent {
    std::coroutine_handle<> handle{};
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept { handle = h; }
    void await_resume() const noexcept {}
    void resume() {
        if (handle) {
            auto h = handle;
            handle = {};
            h.resume();
        }
    }
};

int main() {
    Machine::Builder builder;
    builder.set_initial(State::Idle);
    builder.on<Start>(State::Idle, State::Active, lsm::create_action<Input, Ctx>());
    builder.on<Stop>(State::Active, State::Done, lsm::create_action<Output, Input, Ctx>("stopped"));

    Machine machine = std::move(builder).build({});

    lsm::co::CancelSource cancel_source;
    lsm::co::Adapter<Machine> adapter(machine, &cancel_source);
    lsm::co::scheduler sched;

    ManualEvent gate;
    adapter.bind_async(State::Idle, State::Active,
        [&machine, &gate, sched](const Input&, Ctx& ctx, lsm::co::CancelToken token, auto&) -> lsm::co::Task<std::optional<Output>> {
            ctx.steps = 1;
            if (machine.state() != State::Active) {
                co_return std::optional<Output>{};
            }
            co_await gate;
            lsm::co::throw_if_cancelled(token);
            co_await sched.post();
            ctx.steps = 2;
            co_return std::optional<Output>{"async"};
        }
    );

    Input start = Input{Start{}};
    auto task = adapter.dispatch_async(start);
    if (!task.await_ready()) {
        task.await_suspend(std::noop_coroutine());
    }
    cancel_source.request_stop();
    gate.resume();
    while (!task.await_ready()) {
        task.await_suspend(std::noop_coroutine());
    }
    bool cancelled = false;
    try {
        task.await_resume();
    } catch (const lsm::co::cancelled_error&) {
        cancelled = true;
    }

    std::cout << "state=" << static_cast<int>(machine.state()) << " steps=" << machine.context().steps << " cancelled=" << cancelled << "\n";

    Input stop = Input{Stop{}};
    machine.dispatch(stop);
    std::cout << "final=" << static_cast<int>(machine.state()) << "\n";
    return 0;
}
