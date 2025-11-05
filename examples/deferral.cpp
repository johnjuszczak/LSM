#include <iostream>
#include <optional>
#include <string>
#include <variant>

#include <lsm/core.hpp>

enum class S { Wait, Ready, Done };
struct Data { int v; };
struct Tick {};

using I = std::variant<Data, Tick>;
using O = std::string;
struct C { int n = 0; };

using M = lsm::Machine<S, I, O, C>;

int main() {
    M::Builder B;
    B.set_initial(S::Wait)
     .enable_deferral(true)
     .from(S::Wait)
        .on<Data>()
        .suppress_enter_exit(false)
        .defer(true)
        .to(S::Ready);

    B.from(S::Ready)
        .on<Data>()
        .action([](const Data& d, C&) -> std::optional<O> { return std::optional<O>{ std::to_string(d.v) }; })
        .to(S::Done);

    M m = std::move(B).build({});
    auto out = m.dispatch(I{Data{7}});
    std::cout << (out ? *out : std::string("<none>")) << "\n";
    std::cout << (m.state() == S::Done ? "Done" : "Other") << "\n";
    return 0;
}

