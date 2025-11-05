#include <cassert>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

#include <lsm/core.hpp>
#include <lsm/cosm.hpp>

namespace
{
struct Fire {};
struct Plain {};
struct Missing {};

enum class State { Idle, Stage, Done };

using Input = std::variant<Fire, Plain, Missing>;
using Output = int;

using Machine = lsm::Machine<State, Input, Output, struct Context>;

struct Context
{
    int attempts = 0;
    int awaited = 0;
    int emit_value = 0;
    bool produce_emit = false;
    bool produce_completion = false;
    bool throw_in_then = false;
    bool stop_in_await = false;
    bool stop_in_backoff = false;
};

Machine make_machine()
{
    Machine::Builder builder;
    builder.set_initial(State::Idle);
    builder.on<Fire>(State::Idle, State::Stage, lsm::create_action<Input, Context>());
    builder.on<Plain>(State::Idle, State::Idle, lsm::create_action<Input, Context>());
    builder.completion(State::Stage)
        .action([](Context& ctx) -> std::optional<Output> {
            if(ctx.produce_completion)
            {
                return std::optional<Output>{ctx.emit_value + 1};
            }
            return std::optional<Output>{};
        })
        .to(State::Done);
    return std::move(builder).build(Context{});
}

void attach_pipeline(lsm::co::Adapter<Machine>& adapter, Context& ctx, lsm::co::CancelSource& source)
{
    adapter.from(State::Idle)
        .on<Fire>()
        .to(State::Stage)
        .await([&ctx, &source](const Input&, Context&, lsm::co::CancelToken, auto&) -> lsm::co::Task<void> {
            ctx.awaited += 1;
            if(ctx.stop_in_await)
            {
                source.request_stop();
            }
            co_return;
        })
        .then([&ctx](const Input&, Context&, lsm::co::CancelToken, auto&) -> lsm::co::Task<std::optional<Output>> {
            ctx.attempts += 1;
            if(ctx.throw_in_then)
            {
                throw std::logic_error("effect explosion");
            }
            if(ctx.produce_emit)
            {
                co_return std::optional<Output>{ctx.emit_value};
            }
            co_return std::optional<Output>{};
        })
        .retry(2, [&ctx, &source](int attempt, const Input&, Context&, lsm::co::CancelToken, auto&) -> lsm::co::Task<void> {
            if(ctx.stop_in_backoff && attempt == 1)
            {
                source.request_stop();
            }
            co_return;
        })
        .attach();
}

template <class Task>
void drive(Task& task)
{
    while(!task.await_ready())
    {
        task.await_suspend(std::noop_coroutine());
    }
}

} // namespace

static lsm::co::Task<void> make_void_task()
{
    co_return;
}

int main()
{
    // Exercise Task<void> move constructor and assignment.
    // Scheduler immediate awaiter coverage and ancillary helpers.
    {
        auto task = make_void_task();
        drive(task);
        task.await_resume();
    }
    lsm::co::scheduler sched;
    auto immediate = sched.post();
    immediate.await_ready();
    immediate.await_suspend(std::noop_coroutine());
    immediate.await_resume();

    lsm::co::cancelled_error err;
    assert(std::string(err.what()) == "lsm::co cancelled");

    // Successful async pipeline returning emit value.
    {
        lsm::co::CancelSource source;
        Machine machine = make_machine();
        lsm::co::Adapter<Machine> adapter(machine, &source);
        auto& ctx = machine.context();
        attach_pipeline(adapter, ctx, source);
        ctx.emit_value = 99;
        ctx.produce_emit = true;

        auto task = adapter.dispatch_async(Input{Fire{}});
        drive(task);
        auto result = task.await_resume();
        assert(result && *result == 99);
        assert(ctx.attempts == 1);
        assert(machine.state() == State::Done);
    }

    // Completion path supplying output when effect yields nullopt.
    {
        lsm::co::CancelSource source;
        Machine machine = make_machine();
        lsm::co::Adapter<Machine> adapter(machine, &source);
        auto& ctx = machine.context();
        attach_pipeline(adapter, ctx, source);
        ctx.emit_value = 7;
        ctx.produce_completion = true;

        auto task = adapter.dispatch_async(Input{Fire{}});
        drive(task);
        auto result = task.await_resume();
        assert(result && *result == 8);
        assert(machine.state() == State::Done);
    }

    // Transition without async bindings hits commit branch.
    {
        lsm::co::CancelSource source;
        Machine machine = make_machine();
        lsm::co::Adapter<Machine> adapter(machine, &source);
        auto& ctx = machine.context();
        attach_pipeline(adapter, ctx, source);
        auto task = adapter.dispatch_async(Input{Plain{}});
        drive(task);
        auto result = task.await_resume();
        assert(!result);
        assert(machine.state() == State::Idle);
    }

    // Input with no matching transition exercises select failure branch.
    {
        lsm::co::CancelSource source;
        Machine machine = make_machine();
        lsm::co::Adapter<Machine> adapter(machine, &source);
        auto& ctx = machine.context();
        attach_pipeline(adapter, ctx, source);
        auto task = adapter.dispatch_async(Input{Missing{}});
        drive(task);
        auto result = task.await_resume();
        assert(!result);
        assert(machine.state() == State::Idle);
    }

    // Exception path through dispatch_async catch block.
    {
        lsm::co::CancelSource source;
        Machine machine = make_machine();
        lsm::co::Adapter<Machine> adapter(machine, &source);
        auto& ctx = machine.context();
        attach_pipeline(adapter, ctx, source);
        ctx.throw_in_then = true;
        bool caught = false;
        auto task = adapter.dispatch_async(Input{Fire{}});
        try
        {
            drive(task);
            task.await_resume();
        } catch(const std::logic_error&)
        {
            caught = true;
        }
        assert(caught);
        source.reset();
    }

    // Cancellation while executing fragment short-circuits retry loop body.
    {
        lsm::co::CancelSource source;
        Machine machine = make_machine();
        lsm::co::Adapter<Machine> adapter(machine, &source);
        auto& ctx = machine.context();
        attach_pipeline(adapter, ctx, source);
        ctx.stop_in_await = true;
        auto task = adapter.dispatch_async(Input{Fire{}});
        drive(task);
        auto result = task.await_resume();
        assert(!result);
        assert(source.token().stop_requested());
        source.reset();
    }

    // Cancellation triggered from backoff hits outer loop stop condition.
    {
        lsm::co::CancelSource source;
        Machine machine = make_machine();
        lsm::co::Adapter<Machine> adapter(machine, &source);
        auto& ctx = machine.context();
        attach_pipeline(adapter, ctx, source);
        ctx.stop_in_backoff = true;
        auto task = adapter.dispatch_async(Input{Fire{}});
        drive(task);
        auto result = task.await_resume();
        assert(!result);
        assert(source.token().stop_requested());
        source.reset();
    }

    // Exhaust retries without result to reach final co_return empty optional.
    {
        lsm::co::CancelSource source;
        Machine machine = make_machine();
        lsm::co::Adapter<Machine> adapter(machine, &source);
        auto& ctx = machine.context();
        attach_pipeline(adapter, ctx, source);
        auto task = adapter.dispatch_async(Input{Fire{}});
        drive(task);
        auto result = task.await_resume();
        assert(!result);
        assert(ctx.attempts == 2);
    }

    {
        Machine machine = make_machine();
        Machine::Effect::Action empty_transition{};
        Input sample{Plain{}};
        auto transition_out = Machine::Effect::invoke_transition_action(machine, empty_transition, sample, machine.context());
        assert(!transition_out);

        Machine::Effect::CompletionAction empty_completion{};
        auto completion_out = Machine::Effect::invoke_completion_action(machine, empty_completion, machine.context());
        assert(!completion_out);
    }

    return 0;
}
