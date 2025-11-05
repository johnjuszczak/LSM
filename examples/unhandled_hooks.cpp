#include <cassert>
#include <iostream>
#include <optional>
#include <string>
#include <variant>

#include <lsm/core.hpp>

enum class State { A, B };
struct Go {};
struct Unknown { int v; };

using Input = std::variant<Go, Unknown>;
using Output = std::string;

struct Ctx {
    int machine_hits = 0;
    int state_hits   = 0;
};

using M = lsm::Machine<State, Input, Output, Ctx>;

int main() {
    M::Builder B;
    B.set_initial(State::A)
     .on_unhandled([](Ctx& c, const State& s, const Input& in){
         (void)s; (void)in; ++c.machine_hits; std::cout << "[hook] machine-unhandled\n"; })
     .on_unhandled(State::B, [](Ctx& c, const State& s, const Input& in){
         (void)s; (void)in; ++c.state_hits; std::cout << "[hook] state-unhandled(B)\n"; })
     .on<Go>(State::A, State::B, lsm::create_action<Output, Input, Ctx>("go"));

    M m = std::move(B).build({});

    auto out1 = m.dispatch(Input{Unknown{42}});
    assert(!out1); assert(m.context().machine_hits == 1); assert(m.context().state_hits == 0);

    auto out2 = m.dispatch(Input{Go{}});
    assert(out2 && *out2 == "go");

    auto out3 = m.dispatch(Input{Unknown{7}});
    assert(!out3); assert(m.context().machine_hits == 1); assert(m.context().state_hits == 1);

    auto out4 = m.dispatch(Input{Unknown{8}});
    assert(!out4); assert(m.context().machine_hits == 1); assert(m.context().state_hits == 2);

    std::cout << "OK\n";
    return 0;
}
