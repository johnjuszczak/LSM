#include <iostream>
#include <optional>
#include <string>
#include <variant>

#include <lsm/core.hpp>

enum class LightState { MainGreen, MainYellow, PedestrianWalk, AllRed };

struct Tick {};
struct EmergencyDetected {};
struct EmergencyCleared {};

using Input  = std::variant<Tick, EmergencyDetected, EmergencyCleared>;
using Output = std::string;

struct Context {
    bool pedestrian_waiting = false;
    bool emergency_active   = false;
    int  cycle_count        = 0;
};

using TrafficMachine = lsm::Machine<LightState, Input, Output, Context>;

static const char* to_cstr(LightState s) {
    switch (s) {
        case LightState::MainGreen:       return "MainGreen";
        case LightState::MainYellow:      return "MainYellow";
        case LightState::PedestrianWalk:  return "PedestrianWalk";
        case LightState::AllRed:          return "AllRed";
    }
    return "?";
}

static const char* input_name(const Input& in) {
    if (std::holds_alternative<Tick>(in))             return "Tick";
    if (std::holds_alternative<EmergencyDetected>(in))return "EmergencyDetected";
    if (std::holds_alternative<EmergencyCleared>(in)) return "EmergencyCleared";
    return "Unknown";
}

static void step(TrafficMachine& m, const Input& in, const char* label) {
    auto out = m.dispatch(in);
    std::cout << "[" << label << "] Input=" << input_name(in);
    if (out) std::cout << ", Output=\"" << *out << "\"";
    else     std::cout << ", Output=<none>";
    std::cout << ", State=" << to_cstr(m.state()) << "\n";
}

int main() {
    TrafficMachine::Builder B;

    B.set_initial(LightState::MainGreen)
        .on_enter(LightState::MainGreen, [](Context& ctx, const LightState&, const LightState&, const Input*) {
            ++ctx.cycle_count;
            std::cout << "  -> Enter MainGreen (cycle " << ctx.cycle_count << ")\n";
        })
        .on_enter(LightState::PedestrianWalk, [](Context& ctx, const LightState&, const LightState&, const Input*) {
            ctx.pedestrian_waiting = false;
            std::cout << "  -> Enter PedestrianWalk (clearing pedestrian wait)\n";
        })
        .on_enter(LightState::AllRed, [](Context& ctx, const LightState&, const LightState&, const Input*) {
            std::cout << "  -> Enter AllRed";
            if (ctx.emergency_active) std::cout << " (emergency active)";
            std::cout << "\n";
        })
        .on_exit(LightState::AllRed, [](Context&, const LightState&, const LightState&, const Input*) {
            std::cout << "  -> Exit AllRed\n";
        });

    // --------------------- MainGreen transitions -------------------------
    {
        TrafficMachine::Transition t;
        t.from = LightState::MainGreen;
        t.to = LightState::AllRed;
        t.suppress_enter_exit = false;
        t.priority = 2;
        t.guard = TrafficMachine::Guard{[](const Input& in, const Context& ctx) {
            return std::holds_alternative<Tick>(in) && ctx.emergency_active;
        }};
        t.action = TrafficMachine::Action{[](const Input&, Context&) {
            return std::optional<Output>{"Priority -> All red for emergency"};
        }};
        B.add_transition(std::move(t));
    }
    {
        TrafficMachine::Transition t;
        t.from = LightState::MainGreen;
        t.to = LightState::PedestrianWalk;
        t.suppress_enter_exit = false;
        t.priority = 1;
        t.guard = TrafficMachine::Guard{[](const Input& in, const Context& ctx) {
            return std::holds_alternative<Tick>(in) && ctx.pedestrian_waiting && !ctx.emergency_active;
        }};
        t.action = TrafficMachine::Action{[](const Input&, Context&) {
            return std::optional<Output>{"Priority -> Serving pedestrian"};
        }};
        B.add_transition(std::move(t));
    }

    B.on<Tick>(LightState::MainGreen, LightState::MainYellow,
               lsm::create_action<Output, Input, Context>("Normal -> Yellow"));

    // --------------------- Other deterministic transitions ---------------
    B.on<Tick>(LightState::MainYellow, LightState::AllRed,
               lsm::create_action<Output, Input, Context>("Yellow -> All red"));

    {
        TrafficMachine::Transition t;
        t.from = LightState::AllRed;
        t.to = LightState::AllRed;
        t.suppress_enter_exit = true;
        t.priority = 1;
        t.guard = TrafficMachine::Guard{[](const Input& in, const Context& ctx) {
            return std::holds_alternative<Tick>(in) && ctx.emergency_active;
        }};
        t.action = TrafficMachine::Action{[](const Input&, Context&) {
            return std::optional<Output>{"Holding all red (emergency)"};
        }};
        B.add_transition(std::move(t));
    }

    B.on<Tick>(LightState::AllRed, LightState::MainGreen,
               lsm::create_action<Output, Input, Context>("All red -> Green"));

    B.on<Tick>(LightState::PedestrianWalk, LightState::AllRed,
               lsm::create_action<Output, Input, Context>("Pedestrian walk -> All red"));

    // Emergency events from any state
    B.any()
        .on<EmergencyDetected>()
        .priority(10)
        .action([](const EmergencyDetected&, Context& ctx) -> std::optional<Output> {
            ctx.emergency_active = true;
            return std::optional<Output>{"Emergency detected"};
        })
        .to(LightState::AllRed);

    B.from(LightState::AllRed)
        .on<EmergencyCleared>()
        .suppress_enter_exit(true)
        .action([](const EmergencyCleared&, Context& ctx) -> std::optional<Output> {
            ctx.emergency_active = false;
            return std::optional<Output>{"Emergency cleared"};
        })
        .to(LightState::AllRed);

    TrafficMachine machine = std::move(B).build({});

    std::cout << "Initial state -> " << to_cstr(machine.state()) << "\n";

    // --- Normal cycle ---------------------------------------------------
    step(machine, Tick{}, "Normal cycle: tick 1");
    step(machine, Tick{}, "Normal cycle: tick 2");
    step(machine, Tick{}, "Normal cycle: tick 3");

    // --- Serve pedestrian request --------------------------------------
    std::cout << "\nPedestrian presses the button (flag set in context)\n";
    machine.context().pedestrian_waiting = true;
    step(machine, Tick{}, "Pedestrian cycle: tick 1");
    step(machine, Tick{}, "Pedestrian cycle: tick 2");
    step(machine, Tick{}, "Pedestrian cycle: tick 3");

    // --- Emergency override --------------------------------------------
    std::cout << "\nEmergency vehicle detected\n";
    step(machine, EmergencyDetected{}, "Emergency event");
    step(machine, Tick{}, "Emergency: hold all red");
    step(machine, EmergencyCleared{}, "Emergency cleared");
    step(machine, Tick{}, "Return to service");

    return 0;
}

