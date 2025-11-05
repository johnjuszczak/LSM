#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <variant>

#include <lsm/cosm.hpp>

enum class State { Idle, Waiting, Done };
struct Fetch {};
using Input  = std::variant<Fetch>;
using Output = std::string;

struct Context {
    int attempts = 0;
};

using Machine = lsm::CoMachine<State, Input, Output, Context>;

int main() {
    Machine::Builder builder;
    builder.set_initial(State::Idle);
    builder.on<Fetch>(State::Idle, State::Waiting, lsm::create_action<Input, Context>());

    Machine machine = std::move(builder).build({});
    lsm::co::CancelSource cancel;
    lsm::co::Adapter<Machine> adapter(machine, &cancel);
    lsm::co::scheduler sched;

    adapter.bind_async(State::Idle, State::Waiting,
        [&sched](const Input&, Context& ctx, lsm::co::CancelToken token, auto&) -> lsm::co::Task<std::optional<Output>> {
            for (int attempt = 1; attempt <= 3; ++attempt) {
                ctx.attempts = attempt;
                if (token.stop_requested()) co_return std::optional<Output>{"cancelled"};
                if (attempt == 3) {
                    co_return std::optional<Output>{"success after retry"};
                }
                co_await sched.post();
            }
            co_return std::optional<Output>{};
        }
    );

    auto task = adapter.dispatch_async(Input{Fetch{}});
    while (!task.await_ready()) {
        task.await_suspend(std::noop_coroutine());
    }
    auto result = task.await_resume();
    std::cout << "attempts=" << machine.context().attempts << " result="
              << (result ? *result : std::string{"<none>"}) << "\n";

    return 0;
}
