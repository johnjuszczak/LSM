#include <cassert>
#include <optional>
#include <stdexcept>
#include <variant>

#include <lsm/core.hpp>
#include <lsm/cosm.hpp>

enum class State { Idle, Active, Done };
struct Start {};
using Input = std::variant<Start>;
using Output = int;
struct Ctx {
    int value = 0;
};

using Machine = lsm::Machine<State, Input, Output, Ctx>;

static Machine make_basic_machine() {
    Machine::Builder builder;
    builder.set_initial(State::Idle);
    builder.on<Start>(State::Idle, State::Active, lsm::create_action<Input, Ctx>());
    return std::move(builder).build({});
}

struct ManualGate {
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

static void test_commit_before_effect() {
    Machine machine = make_basic_machine();
    auto check = make_basic_machine();
    auto direct = check.dispatch(Input{Start{}});
    assert(!direct);
    assert(check.state() == State::Active);
    lsm::co::Adapter<Machine> adapter(machine);
    adapter.bind_async(State::Idle, State::Active,
        [&machine](const Input&, Ctx& ctx, lsm::co::CancelToken, auto&) -> lsm::co::Task<std::optional<Output>> {
            ctx.value = (machine.state() == State::Active) ? 1 : -1;
            co_return std::optional<Output>{};
        });
    auto task = adapter.dispatch_async(Input{Start{}});
    while (!task.await_ready()) {
        task.await_suspend(std::noop_coroutine());
    }
    auto out = task.await_resume();
    assert(!out);
    assert(machine.state() == State::Active);
    assert(machine.context().value == 1);
}

static void test_cancellation_propagates() {
    Machine machine = make_basic_machine();
    lsm::co::CancelSource source;
    lsm::co::Adapter<Machine> adapter(machine, &source);
    ManualGate gate;
    adapter.bind_async(State::Idle, State::Active,
        [&gate](const Input&, Ctx& ctx, lsm::co::CancelToken token, auto&) -> lsm::co::Task<std::optional<Output>> {
            co_await gate;
            ctx.value = 7;
            lsm::co::throw_if_cancelled(token);
            co_return std::optional<Output>{};
        }
    );
    auto task = adapter.dispatch_async(Input{Start{}});
    if (!task.await_ready()) {
        task.await_suspend(std::noop_coroutine());
    }
    source.request_stop();
    auto check = source.token();
    assert(check.stop_requested());
    gate.resume();
    while (!task.await_ready()) {
        task.await_suspend(std::noop_coroutine());
    }
    bool caught = false;
    try {
        task.await_resume();
    } catch (const lsm::co::cancelled_error&) {
        caught = true;
    }
    assert(caught);
    assert(machine.state() == State::Active);
    assert(machine.context().value == 7);
}

static void test_exception_propagates() {
    Machine machine = make_basic_machine();
    lsm::co::Adapter<Machine> adapter(machine);
    adapter.bind_async(State::Idle, State::Active,
        [](const Input&, Ctx&, lsm::co::CancelToken, auto&) -> lsm::co::Task<std::optional<Output>> {
            throw std::runtime_error("boom");
        }
    );
    auto task = adapter.dispatch_async(Input{Start{}});
    bool caught = false;
    try {
        while (!task.await_ready()) {
            task.await_suspend(std::noop_coroutine());
        }
        task.await_resume();
    } catch (const std::runtime_error&) {
        caught = true;
    }
    assert(caught);
}

int main() {
    test_commit_before_effect();
    test_cancellation_propagates();
    test_exception_propagates();
    return 0;
}
