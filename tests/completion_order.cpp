#include <cassert>
#include <optional>
#include <string>
#include <variant>

#include <lsm/core.hpp>

enum class S { Start, Setup, PathA, PathB };
struct Begin {};
using Input = std::variant<Begin>;
using Output = std::string;
using Machine = lsm::Machine<S, Input, Output, std::monostate>;

static Machine make_router() {
    Machine::Builder builder;
    builder.set_initial(S::Start);
    builder.from(S::Start)
        .on<Begin>()
        .to(S::Setup);
    builder.completion(S::Setup)
        .guard([](const std::monostate&) { return true; })
        .priority(5)
        .action([](std::monostate&) -> std::optional<Output> { return std::optional<Output>{"A"}; })
        .to(S::PathA);
    builder.completion(S::Setup)
        .guard([](const std::monostate&) { return true; })
        .priority(0)
        .action([](std::monostate&) -> std::optional<Output> { return std::optional<Output>{"B"}; })
        .to(S::PathB);
    return std::move(builder).build({});
}

enum class LS { LoopA, LoopB };
using LoopInput = std::variant<std::monostate>;
using LoopMachine = lsm::Machine<LS, LoopInput, std::monostate, std::monostate>;

static LoopMachine make_loop() {
    LoopMachine::Builder builder;
    builder.set_initial(LS::LoopA);
    builder.completion(LS::LoopA)
        .to(LS::LoopB);
    builder.completion(LS::LoopB)
        .to(LS::LoopA);
    return std::move(builder).build({});
}

int main() {
    auto router = make_router();
    auto out = router.dispatch(Input{Begin{}});
    assert(out && *out == "A");
    assert(router.state() == S::PathA);

    auto loop = make_loop();
    assert(loop.state() == LS::LoopA);

    return 0;
}

