#include <cassert>
#include <chrono>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <lsm/core.hpp>
#include <lsm/cosm.hpp>

enum class State { Idle, Active, Done };
struct Kick {};
struct Unknown {};

using Input = std::variant<Kick, Unknown>;
using Output = int;

struct Context
{
    int attempts = 0;
    int awaited = 0;
    int emitted = 0;
    std::vector<int> backoffs;
};

using Machine = lsm::Machine<State, Input, Output, Context>;

static Machine make_machine()
{
    Machine::Builder builder;
    builder.set_initial(State::Idle);
    builder.on<Kick>(State::Idle, State::Active, lsm::create_action<Input, Context>());
    builder.on<Unknown>(State::Idle, State::Idle, lsm::create_action<Input, Context>());
    builder.completion(State::Active)
        .to(State::Done);
    return std::move(builder).build(Context{});
}

static void run(lsm::co::Task<std::optional<Output>>& task)
{
    while(!task.await_ready())
    {
        task.await_suspend(std::noop_coroutine());
    }
}

static void run(lsm::co::Task<void>& task)
{
    while(!task.await_ready())
    {
        task.await_suspend(std::noop_coroutine());
    }
}

static lsm::co::Task<void> await_cancel(lsm::co::CancelSource& src)
{
    co_await lsm::co::cancelled(src.token());
    co_return;
}

int main()
{
    Machine machine = make_machine();
    lsm::co::CancelSource source;
    lsm::co::Adapter<Machine> adapter(machine, &source);
    lsm::co::scheduler sched;

    adapter.from(State::Idle)
        .on<Kick>()
        .to(State::Active)
        .await([&](const Input&, Context& ctx, lsm::co::CancelToken tok, auto&) -> lsm::co::Task<void> {
            ctx.awaited += 1;
            co_await sched.post();
            co_await sched.yield();
            co_await sched.sleep_for(std::chrono::milliseconds(0));
            if(tok.stop_requested()) co_return;
            co_return;
        })
        .then([&](const Input&, Context& ctx, lsm::co::CancelToken, auto&) -> lsm::co::Task<std::optional<Output>> {
            ctx.attempts += 1;
            if(ctx.attempts < 3)
            {
                co_return std::optional<Output>{};
            }
            co_return std::optional<Output>{42};
        })
        .retry(3, [&](int attempt, const Input&, Context& ctx, lsm::co::CancelToken, auto&) -> lsm::co::Task<void> {
            ctx.backoffs.push_back(attempt);
            co_return;
        })
        .emit([&](const Input&, Context& ctx, auto&) -> Output {
            ctx.emitted = 99;
            return 99;
        })
        .attach();

    auto task = adapter.dispatch_async(Input{Kick{}});
    run(task);
    auto result = task.await_resume();
    assert(result && *result == 99);
    assert(machine.context().attempts == 3);
    assert(machine.context().awaited == 3);
    assert(machine.context().emitted == 99);
    assert((machine.context().backoffs == std::vector<int>{1, 2}));
    assert(machine.state() == State::Done);

    auto idle_task = adapter.dispatch_async(Input{Unknown{}});
    run(idle_task);
    auto idle_result = idle_task.await_resume();
    assert(!idle_result);
    assert(machine.state() == State::Done);

    lsm::co::CancelSource local;
    auto safe = await_cancel(local);
    run(safe);
    safe.await_resume();

    local.request_stop();
    auto cancelled = await_cancel(local);
    run(cancelled);
    bool caught = false;
    try
    {
        cancelled.await_resume();
    } catch(const lsm::co::cancelled_error&)
    {
        caught = true;
    }
    assert(caught);
    local.reset();
    assert(!local.token().stop_requested());

    return 0;
}
