#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <variant>

#include <lsm/cosm.hpp>

enum class State { Idle, Active, Timeout, Completed };
struct Start {};
using Input  = std::variant<Start>;
using Output = std::string;

struct Context {
    bool timed_out = false;
};

using Machine = lsm::CoMachine<State, Input, Output, Context>;

struct FakeTimer {
    std::coroutine_handle<> handle{};
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept { handle = h; }
    void await_resume() const noexcept {}
    void fire() {
        if (handle) {
            auto resume = handle;
            handle = {};
            resume.resume();
        }
    }
};

int main() {
    Machine::Builder builder;
    builder.set_initial(State::Idle);
    builder.on<Start>(State::Idle, State::Active, lsm::create_action<Input, Context>());
    builder.on<Start>(State::Timeout, State::Idle, lsm::create_action<Output, Input, Context>("reset"));

    Machine machine = std::move(builder).build({});

    lsm::co::CancelSource cancel_source;
    lsm::co::Adapter<Machine> adapter(machine, &cancel_source);
    FakeTimer timer;

    adapter.bind_async(State::Idle, State::Active,
        [&timer](const Input&, Context& ctx, lsm::co::CancelToken token, auto&) -> lsm::co::Task<std::optional<Output>> {
            ctx.timed_out = false;
            co_await timer;
            if (token.stop_requested()) {
                co_return std::optional<Output>{"cancelled"};
            }
            ctx.timed_out = true;
            co_return std::optional<Output>{"timeout"};
        }
    );

    auto task = adapter.dispatch_async(Input{Start{}});
    task.await_suspend(std::noop_coroutine());
    timer.fire();
    while (!task.await_ready()) {
        task.await_suspend(std::noop_coroutine());
    }
    auto out = task.await_resume();
    if (out) {
        std::cout << *out << "\n";
    }
    std::cout << "timed_out=" << machine.context().timed_out << "\n";

    return 0;
}

