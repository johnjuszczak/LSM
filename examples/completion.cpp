#include <iostream>
#include <optional>
#include <string>
#include <variant>

#include <lsm/core.hpp>

enum class S { Idle, Step1, Step2, Done };
struct Start {};

using I = std::variant<Start>;
using O = std::string;
struct C { int steps = 0; };

using M = lsm::Machine<S, I, O, C, lsm::policy::move>;

int main() {
    M::Builder b;
    b.set_initial(S::Idle);
    b.from(S::Idle)
        .on<Start>()
        .to(S::Step1);

    b.completion(S::Step1)
        .action([](C& ctx) -> std::optional<O> {
            ctx.steps = 1;
            return std::optional<O>{"step1"};
        })
        .to(S::Step2);

    b.completion(S::Step2)
        .action([](C& ctx) -> std::optional<O> {
            ctx.steps = 2;
            return std::optional<O>{"step2"};
        })
        .to(S::Done);

    auto m = std::move(b).build({});
    auto out = m.dispatch(I{Start{}});
    if (out) std::cout << *out << "\n";
    std::cout << (m.state() == S::Done ? "Done" : "Other") << "\n";
    std::cout << m.context().steps << "\n";
    return 0;
}
