# Lime State Machine

A modern, header-only finite state machine library focused on clarity and computational model accurate virtualization.

---

## Highlights

- Header-only, zero-dependency core.
- Strongly-typed states, events, and actions.
- Table-driven or fluent transitions.
- Predictable execution model: `on_event -> transition -> actions -> state update`.
- No macros required; constexpr-friendly configuration.
- Coroutine based machine available.

---

## Install

### CMake (FetchContent)
```cmake
include(FetchContent)
FetchContent_Declare(
  lsm
  GIT_REPOSITORY https://github.com/johnjuszczak/LSM.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(lsm)
```

### Manual

Drop the include/lsm/ folder into your project and add it to your include paths.

---

## Building
This project uses CMake presets. Standard development targets `win-msvc-23`, but CI verifies builds across `clang-cl`, `g++`, and `clang++`. Use the preset that matches your toolchain:

```
cmake --preset win-msvc-23
cmake --build --preset build-win-msvc-23-debug
```

Classic configure/build also works—just remember to enable the examples target:

```
cmake -S . -B build -DLSM_BUILD_EXAMPLES=ON
cmake --build build --config Release
```

`LSM_BUILD_TESTS=ON` is optional if you also want the test suite.

---

## Examples

Build with a C++23 compiler and run the resulting binaries:

- `examples/door.cpp`: Basic API and fluent DSL
- `examples/traffic_light_priorities.cpp`: Prioritized transitions and any-state
- `examples/processing_queue.cpp`: Internal queue and dispatch_all
- `examples/unhandled_hooks.cpp`: Unhandled-event hooks (machine-level and state override)
- `examples/deferral.cpp`: Event deferral and drain-on-enter
- `examples/completion.cpp`: Completion transitions on enter
- `examples/turnstile.cpp`: Minimal deterministic turnstile
- `examples/deferral_gate.cpp`: Deferred events drained on readiness
- `examples/completion_splitter.cpp`: Guarded completion splitter
- `examples/publisher_queue.cpp`: Minimal publisher policy example
- `examples/coroutine_retry.cpp`: Coroutine retry with backoff
- `examples/coroutine_timeout.cpp`: Coroutine timeout path
- `examples/coroutine_cancellation.cpp`: Coroutine commit-before with cancellation
- `examples/coroutine_publisher.cpp`: Coroutine publisher + adapter

### Basic Usage

- Core types: `Machine<State, Input, Output, Ctx>` with `dispatch(input)` and optional `update()` ticks.
- Define initial state and transitions; actions optionally return `std::optional<Output>` (ReturnOutput policy) or publish via a publisher (Publisher policy).
- See `examples/turnstile.cpp` and `examples/door.cpp` for minimal and slightly richer end-to-end usage.

### Builder APIs

- Direct (table-style): call `builder.on<Event>(from, to, action, guard, ...)` and state hooks via `on_enter/on_do/on_exit`. See `examples/door.cpp` (Example A/C).
- Fluent DSL: `builder.from(state).on<T>().guard(...).action(...).to(state)` and type tags via `on(type_c<T>)`. See `examples/door.cpp` (Example B).

### Priorities & Any-State

- Demonstrates prioritized transitions and any-state edges with clear resolution order. See `examples/traffic_light_priorities.cpp`.

### Internal Queue

- Shows event queuing and `dispatch_all`-style processing inside a state. See `examples/processing_queue.cpp`.

### Publisher Policy

- Use an effect policy parameter to indicate if state hooks should return an output type, or if machine output solely occurs through events: `policy::ReturnOutput<Out>` (default) optional-return behavior; `policy::Publisher<Pub>` routes effects through a publisher object supplied via `Builder::set_publisher(...)`.
- Helpers live under `lsm::publisher`: `Queue<Storage>` wraps a push-back container; `NullPublisher` is a no-op; `Concept` is a convenience concept for custom publishers.
- In publisher mode, actions/`on_do`/completions receive `(ctx, ..., publisher)` and publish via `publisher.publish(value)` rather than returning `std::optional<Output>`.

### Unhandled Event Hooks

Attach a hook that fires when no transition matches the current state and input. Define a machine-level hook via the builder, and optionally override per-state.

```
using M = lsm::Machine<State, Input, Output, Ctx>;
M::Builder B;
B.set_initial(State::A)
 .on_unhandled([](Ctx& ctx, const State& s, const Input& in) {
     std::cout << "Invalid transition" << std::endl;
 })
 .on_unhandled(State::B, [](Ctx& ctx, const State& s, const Input& in) {
     std::cout << "Invalid transition from A to B" << std::endl;
 });

M m = std::move(B).build({});
```

Notes:
- Hooks are invoked after transition search fails and must not affect state.
- Exceptions from hooks are swallowed to preserve dispatch behavior.

### Event Deferral

Enable deferral and mark a transition to defer the current input to the destination state's queue. Deferred inputs are drained on entering a state.

```
using M = lsm::Machine<State, Input, Output, Ctx>;
M::Builder B;
B.set_initial(State::Wait)
 .enable_deferral(true)
 .from(State::Wait)
    .on<SomeEvent>()
    .defer(true)
    .to(State::Ready);
```

### Completion Transitions

Completion edges fire automatically after entering a state and are evaluated before processing queued inputs.

```
using M = lsm::Machine<State, Input, Output, Ctx>;
M::Builder B;
B.set_initial(State::Idle);
B.from(State::Idle)
 .on<Start>()
 .to(State::Phase1);
B.completion(State::Phase1)
 .action([](Ctx& ctx) -> std::optional<Output> {
     (void)ctx;
     return std::optional<Output>{"phase1"};
 })
 .to(State::Done);
```

### Callable Policies

`lsm::policy::copy` indicates that captures are copyable. `lsm::policy::move` indicates that captures are moveable. Select the policy via the machine template parameter.

```
using MoveMachine = lsm::Machine<State, Input, Output, Ctx, lsm::policy::move>;
MoveMachine::Builder builder;
builder.set_initial(State::Idle)
       .from(State::Idle)
       .on<Start>()
       .action([ptr = std::make_unique<int>(42)](const Start&, Ctx&) -> std::optional<Output> {
           return std::optional<Output>{*ptr};
       })
       .to(State::Done);
auto machine = std::move(builder).build({});
```

### Coroutine Semantics

`lsm::co::Adapter` commits state before invoking async effects. Each bound effect receives `(const Input&, Context&, CancelToken)` and may return `std::optional<Output>`. Cancellation is cooperative via `CancelSource` and `CancelToken`; use `throw_if_cancelled(token)` or `co_await cancelled(token)` to respect requests. A minimal scheduler facade (`lsm::co::scheduler`) offers no-op `post`, `yield`, and `sleep_for` helpers that don't introduce a runtime.

