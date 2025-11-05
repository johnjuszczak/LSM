#include <iostream>
#include <string>
#include <variant>
#include <lsm/core.hpp>

// ----------------- variant-based Input -----------------
enum class DoorState { Closed, Open, Locked };

// Inputs
struct Push {};
struct Pull {};
struct Lock {};
struct Unlock {};

// std::variant based input
using Input  = std::variant<Push, Pull, Lock, Unlock>;

// simple output as string to print
using Output = std::string;

struct Ctx {
    bool has_key = false;
    int  ticks_in_open = 0;
};

static const char* to_cstr(DoorState s) {
    switch (s) {
        case DoorState::Closed: return "Closed";
        case DoorState::Open:   return "Open";
        case DoorState::Locked: return "Locked";
    }
    return "?";
}

using DoorMachine = lsm::Machine<DoorState, Input, Output, Ctx>;

// Demonstration handler object with on_enter/on_do for ReturnOutput policy
struct DoorClosedHandler {
    void on_enter(Ctx&, const DoorState& from, const DoorState& to, const Input*) {
        if(from != to) {
            std::cout << "  [handler] Closed:on_enter\n";
        }
    }
    std::optional<Output> on_do(Ctx&, const DoorState&) {
        return std::optional<Output>{"  [handler] Closed:on_do tick\n"};
    }
};

static void step(DoorMachine& m, const Input& in, const char* label) {
    auto out = m.dispatch(in);
    std::cout << "[" << label << "] ";

    if (std::holds_alternative<Push>(in))          std::cout << "Input=Push, ";
    else if (std::holds_alternative<Pull>(in))     std::cout << "Input=Pull, ";
    else if (std::holds_alternative<Lock>(in))     std::cout << "Input=Lock, ";
    else if (std::holds_alternative<Unlock>(in))   std::cout << "Input=Unlock, ";

    if (out) std::cout << "Output=\"" << *out << "\", ";
    else     std::cout << "Output=<none>, ";
    std::cout << "State=" << to_cstr(m.state()) << "\n";
}

static void step(DoorMachine& m, const char* label) {
    auto out = m.update();
    std::cout << "[" << label << "] Step=Update, ";
    if(out) std::cout << "Output=\"" << *out << "\", ";
    std::cout << "State=" << to_cstr(m.state()) << std::endl;
}

int main() {
    // =========================================================================
    // Example A: General API usage (on<T>, on_any<T>, internal)
    // =========================================================================
    {
        std::cout << "=== Example A: Direct API ===\n";
        DoorMachine::Builder B;

        // set initial machine state
        B.set_initial(DoorState::Closed)
        .on_enter(DoorState::Closed, [](Ctx&, const DoorState&, const DoorState&, const Input*) {
            std::cout << "  [hook] Enter Closed\n";
        })
        .on_do(DoorState::Closed, [](Ctx&, const DoorState&) {
            return "  [hook] Door currently closed\n";
        })
        .on_exit(DoorState::Closed, [](Ctx&, const DoorState&, const DoorState&, const Input*) {
            std::cout << "  [hook] Exit Closed\n";
        });

        // defining state enter/do/exit functionality for demonstration purposes. All Builder
        // function calls can be chained or called individually
        B.on_enter(DoorState::Open, [](Ctx& c, const DoorState&, const DoorState&, const Input*) {
            c.ticks_in_open = 0;
            std::cout << "  [hook] Enter Open -> reset tick counter\n";
        })
        .on_do(DoorState::Open, [](Ctx& c, const DoorState&) -> std::optional<Output> {
            ++c.ticks_in_open;
            if (c.ticks_in_open % 2 == 0) return "  [hook] Open creak...\n";
            return std::nullopt;
        })
        .on_exit(DoorState::Open, [](Ctx&, const DoorState&, const DoorState&, const Input*) {
            std::cout << "  [hook] Exit Open\n";
        });

        // Transition specific behavior

        // Closed --on Push--> Open
        B.on<Push>(DoorState::Closed, DoorState::Open,
            lsm::create_action<Output, Input, Ctx>("Pushed Open"));

        // Open --on Pull--> Closed
        B.on<Pull>(DoorState::Open, DoorState::Closed,
            lsm::create_action<Output, Input, Ctx>("Pulled Closed"));

        // Closed --on Lock--> Locked
        B.on<Lock>(DoorState::Closed, DoorState::Locked,
            lsm::create_action<Output, Input, Ctx>("Locked Lock"));

        // Locked --on Unlock--> Closed
        // Context conditional output
        B.on<Unlock>(DoorState::Locked, DoorState::Closed,
            [](const Unlock&, Ctx& c) -> std::optional<Output> {
                return c.has_key ? std::optional<Output>{"Unlocked with key"} : std::nullopt;
            },
            nullptr
        );

        // Demonstrate object-centric on_state binding (by_ref tag)
        DoorClosedHandler closedHandler{};
        B.on_state(DoorState::Closed, closedHandler, lsm::bind::by_ref{});

        DoorMachine m = std::move(B).build(Ctx{ .has_key = true });

        step(m, Push{}, "Example A: step1");         // Closed -> Open
        step(m, Pull{}, "Example A: step2");         // Open -> Closed
        step(m, Lock{}, "Example A: step3");         // Closed -> Locked
        step(m, Unlock{}, "Example A: step4");       // Locked -> Closed (has key)
        step(m, Lock{}, "Example A: step5");         // Closed -> Locked
        m.context().has_key = false;
        step(m, Unlock{}, "Example A: step6");       // Locked -> Locked (no key)
        m.context().has_key = true;
        step(m, Unlock{}, "Example A: step7");       // Locked -> Closed (has key)
        step(m, "Example A: step8");                 // Update Closed
        step(m, Lock{}, "Example A: step9");         // Closed -> Locked
        step(m, "Example A: step10");                // Update Locked
        std::cout << "\n";
    }

    // =========================================================================
    // Example B: Fluent DSL (including type-tag: on(type_c<T>))
    // =========================================================================
    {
        std::cout << "=== Example B: Fluent DSL ===\n";
        DoorMachine::Builder B;

        B.set_initial(DoorState::Closed)
        .on_enter(DoorState::Open, [](Ctx&, const DoorState&, const DoorState&, const Input*) {
            std::cout << "  [hook] Door is now open\n\n";
        });

        // Closed --on Push--> Open (type-tag flavor)
        B.from(DoorState::Closed)
            .on<Push>()
            .action(lsm::create_action<Output, Input, Ctx>("Pushed Open"))
            .to(DoorState::Open);

        // Open --on Pull--> Closed
        B.from(DoorState::Open)
            .on(lsm::type_c<Pull>)
            .action(lsm::create_action<Output, Input, Ctx>("Pulled Closed"))
            .to(DoorState::Closed);

        // Locked --on Unlock--> Closed
        B.from(DoorState::Locked)
            .on(lsm::type_c<Unlock>)
            .action([](const Unlock&, Ctx& c) -> std::optional<Output> {
                if (!c.has_key) return std::nullopt;
                return std::optional<Output>{"Key-Unlock Closed"};
            })
            .to(DoorState::Closed);

        // Closed --on Lock--> Locked
        B.from(DoorState::Closed)
            .on(lsm::type_c<Lock>)
            .action(lsm::create_action<Output, Input, Ctx>("Lock Locked"))
            .to(DoorState::Locked);


        DoorMachine m = std::move(B).build(Ctx{ .has_key = false });

        step(m, Push{}, "Example B.step1");           // Closed -> Open
        step(m, Pull{}, "Example B.step2");           // Open -> Closed
        step(m, Lock{}, "Example B.step3");           // Closed -> Locked
        step(m, Unlock{}, "Example B.step4");         // Locked -> Locked (no key)
        m.context().has_key = true;
        step(m, Unlock{}, "Example B.step5");         // Locked -> Closed
        std::cout << "\n";
    }


    // =========================================================================
    // Example C: add_transition
    // =========================================================================
    {
        std::cout << "=== Example C ===\n";
        DoorMachine::Builder B;

        // set initial machine state
        B.set_initial(DoorState::Closed);

        // defining state enter/do/exit functionality for demonstration purposes. All Builder
        // function calls can be chained or called individually
        B.on_enter(DoorState::Open, [](Ctx& c, const DoorState&, const DoorState&, const Input*) {
            c.ticks_in_open = 0;
            std::cout << "  [hook] Enter Open -> reset tick counter\n";
        })
        .on_do(DoorState::Open, [](Ctx& c, const DoorState&) {
            ++c.ticks_in_open;
            if (c.ticks_in_open % 2 == 0) std::cout << "  [hook] Open creak...\n";
            return std::nullopt;
        })
        .on_exit(DoorState::Open, [](Ctx&, const DoorState&, const DoorState&, const Input*) {
            std::cout << "  [hook] Exit Open\n";
        });

        // Transition specific behavior
        B.add_transition({
            .from = DoorState::Closed,
            .to = DoorState::Open
        });

        // Closed --on Push--> Open
        B.on<Push>(DoorState::Closed, DoorState::Open,
            lsm::create_action<Output, Input, Ctx>("Pushed Open"));

        // Open --on Pull--> Closed
        B.on<Pull>(DoorState::Open, DoorState::Closed,
            lsm::create_action<Output, Input, Ctx>("Pulled Closed"));

        // Closed --on Lock--> Locked
        B.on<Lock>(DoorState::Closed, DoorState::Locked,
            lsm::create_action<Output, Input, Ctx>("Locked Lock"));

        // Locked --on Unlock--> Closed
        // Context conditional output
        B.on<Unlock>(DoorState::Locked, DoorState::Closed,
            [](const Unlock&, Ctx& c) -> std::optional<Output> {
                return c.has_key ? std::optional<Output>{"Unlocked with key"} : std::nullopt;
            },
            nullptr
        );

        DoorMachine m = std::move(B).build(Ctx{ .has_key = true });

        step(m, Push{}, "Example C: step1");         // Closed -> Open
        step(m, "update");
        step(m, "update");
        step(m, "update");
        step(m, "update");
        step(m, "update");
        std::cout << "\n";
    }

    return 0;
}

