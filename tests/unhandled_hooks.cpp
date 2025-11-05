#include <cassert>
#include <optional>
#include <string>
#include <variant>

#include <lsm/core.hpp>

enum class S { X, Y };
struct E {};
struct U {};

using I = std::variant<E, U>;
using O = std::string;
struct C { int m = 0; int s = 0; };

using M = lsm::Machine<S, I, O, C>;

int main() {
    M::Builder B;
    B.set_initial(S::X)
     .on_unhandled([](C& c, const S&, const I&) { ++c.m; })
     .on_unhandled(S::Y, [](C& c, const S&, const I&) { ++c.s; })
     .on<E>(S::X, S::Y, lsm::create_action<O, I, C>("ok"));

    auto m = std::move(B).build({});

    assert(!m.dispatch(I{U{}}));
    assert(m.context().m == 1 && m.context().s == 0);

    auto out = m.dispatch(I{E{}});
    assert(out && *out == "ok");

    assert(!m.dispatch(I{U{}}));
    assert(m.context().m == 1 && m.context().s == 1);

    M::Builder B2;
    B2.set_initial(S::X)
      .on_unhandled([](C&, const S&, const I&) { throw 42; });
    auto m2 = std::move(B2).build({});
    assert(!m2.dispatch(I{U{}}));

    return 0;
}
