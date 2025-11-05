#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <variant>

#include <lsm/core.hpp>

// Demonstrates enqueue/dispatch_all for chained internal work within a state

enum class ProcessState { Idle, Processing, Done };

struct Start { int jobs; };
struct Tick {};

using Input  = std::variant<Start, Tick>;
using Output = std::string;

struct Context {
    int pending = 0;
    std::function<void()> schedule_tick;
};

using ProcessMachine = lsm::Machine<ProcessState, Input, Output, Context>;

static const char* to_cstr(ProcessState s) {
    switch (s) {
        case ProcessState::Idle:       return "Idle";
        case ProcessState::Processing: return "Processing";
        case ProcessState::Done:       return "Done";
    }
    return "?";
}

static void print_step(const char* label, const std::optional<Output>& out, const ProcessMachine& m) {
    std::cout << "[" << label << "] State=" << to_cstr(m.state());
    if (out) std::cout << ", Output=\"" << *out << "\"";
    else     std::cout << ", Output=<none>";
    std::cout << ", Pending=" << m.context().pending << '\n';
}

int main() {
    ProcessMachine::Builder B;

    B.set_initial(ProcessState::Idle)
        .on_enter(ProcessState::Processing, [](Context& ctx, const ProcessState&, const ProcessState&, const Input*) {
            std::cout << "  -> Enter Processing with " << ctx.pending << " job(s)\n";
        })
        .on_enter(ProcessState::Done, [](Context&, const ProcessState&, const ProcessState&, const Input*) {
            std::cout << "  -> Enter Done\n";
        })
        .on_enter(ProcessState::Idle, [](Context& ctx, const ProcessState&, const ProcessState&, const Input*) {
            ctx.pending = 0;
            std::cout << "  -> Enter Idle (reset pending)\n";
        });

    // Idle: Start -> move to Processing and seed work via the queue
    B.on<Start>(ProcessState::Idle, ProcessState::Processing,
        [](const Start& s, Context& ctx) -> std::optional<Output> {
            ctx.pending = s.jobs;
            std::cout << "  action: received Start for " << s.jobs << " job(s)\n";
            if (ctx.pending > 0 && ctx.schedule_tick) ctx.schedule_tick();
            return std::optional<Output>{"Started"};
        });

    // Processing: self-loop Tick consumes work and enqueues the next Tick when needed
    {
        ProcessMachine::Transition t;
        t.from = ProcessState::Processing;
        t.to = ProcessState::Processing;
        t.suppress_enter_exit = true; // internal loop, keep hooks quiet
        t.guard = ProcessMachine::Guard{[](const Input& in, const Context& ctx) {
            return std::holds_alternative<Tick>(in) && ctx.pending > 1;
        }};
        t.action = ProcessMachine::Action{[](const Input&, Context& ctx) -> std::optional<Output> {
            --ctx.pending;
            std::cout << "  action: processed job, remaining=" << ctx.pending << '\n';
            if (ctx.pending > 0 && ctx.schedule_tick) ctx.schedule_tick();
            return std::optional<Output>{"Processed one"};
        }};
        B.add_transition(std::move(t));
    }

    // Completion Tick: when only one job remains, transition to Done
    {
        ProcessMachine::Transition t;
        t.from = ProcessState::Processing;
        t.to = ProcessState::Done;
        t.priority = 1; // check completion before self-loop
        t.guard = ProcessMachine::Guard{[](const Input& in, const Context& ctx) {
            return std::holds_alternative<Tick>(in) && ctx.pending <= 1;
        }};
        t.action = ProcessMachine::Action{[](const Input&, Context& ctx) -> std::optional<Output> {
            if (ctx.pending > 0) --ctx.pending;
            return std::optional<Output>{"All jobs complete"};
        }};
        B.add_transition(std::move(t));
    }

    // Done -> Processing on Start (restart another batch)
    B.on<Start>(ProcessState::Done, ProcessState::Processing,
        [](const Start& s, Context& ctx) -> std::optional<Output> {
            ctx.pending = s.jobs;
            if (ctx.pending > 0 && ctx.schedule_tick) ctx.schedule_tick();
            return std::optional<Output>{"Restart batch"};
        });

    ProcessMachine machine = std::move(B).build({});
    machine.context().schedule_tick = [&machine]() { machine.enqueue(Tick{}); };

    std::cout << "Initial state: " << to_cstr(machine.state()) << "\n";

    // Kick off a batch of three jobs
    auto out = machine.dispatch(Start{3});
    print_step("Start", out, machine);

    // Drain the queued Tick events until the machine goes idle
    auto outputs = machine.dispatch_all();
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        std::cout << "  dispatch_all output[" << i << "] = " << outputs[i] << '\n';
    }
    std::cout << "After dispatch_all: State=" << to_cstr(machine.state())
              << ", Pending=" << machine.context().pending << "\n";

    return 0;
}

