#include <cassert>
#include <optional>
#include <string>
#include <variant>

#include <lsm/core.hpp>

enum class S { A, B, C };
struct Start {};

using I = std::variant<Start>;
using O = std::string;
struct C { int steps = 0; };

using M = lsm::Machine<S, I, O, C>;

int main() {
    M::Builder b;
    b.set_initial(S::A);
    b.from(S::A)
        .on<Start>()
        .to(S::B);
    b.completion(S::B)
        .action([](C& ctx) -> std::optional<O> {
            ctx.steps = 1;
            return std::optional<O>{"step"};
        })
        .to(S::C);
    auto m = std::move(b).build({});
    auto out = m.dispatch(I{Start{}});
    assert(out && *out == "step");
    assert(m.state() == S::C);
    assert(m.context().steps == 1);
    return 0;
}

