#include <cassert>
#include <optional>
#include <string>
#include <variant>

#include <lsm/core.hpp>

enum class S { A, B };
struct Go {};

using Input = std::variant<Go>;
using Output = std::string;
using Machine = lsm::Machine<S, Input, Output, std::monostate>;

int main() {
    Machine::Builder builder;
    builder.set_initial(S::A);
    builder.on<Go>(S::A, S::B, lsm::create_action<Output, Input, std::monostate>("ok"));
    Machine m = std::move(builder).build({});

    Input ev{Go{}};
    auto sel = m.select(ev);
    assert(sel);
    auto out = m.commit(sel, &ev);
    assert(out && *out == "ok");
    assert(m.state() == S::B);
    return 0;
}

