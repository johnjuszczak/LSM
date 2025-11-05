#include <cassert>
#include <memory>
#include <variant>

#include <lsm/core.hpp>

enum class S { A, B };
struct E
{
};
using In = std::variant<E>;
using Out = std::monostate;
struct C
{
    int enter = 0;
    int exit = 0;
    int do_calls = 0;
};

struct Handler
{
    bool* enter_flag{};

    void on_enter(C&, const S&, const S&, const In*)
    {
        if(enter_flag)
        {
            *enter_flag = true;
        }
    }
};

struct HRef
{
    void on_enter(C& c, const S&, const S&, const In*) { ++c.enter; }
    void on_exit(C& c, const S&, const S&, const In*) { ++c.exit; }
};

struct HPtr
{
    std::optional<Out> on_do(C& c, const S&) { ++c.do_calls; return std::nullopt; }
};

int main()
{
    using M = lsm::Machine<S, In, Out, C>;
    M::Builder B;
    B.set_initial(S::A);

    HRef href;
    B.on_state(S::A, href, lsm::detail::by_ref{});

    HPtr hptr;
    B.on_state(S::A, &hptr, lsm::detail::by_ptr{});

    bool initial_enter_called = false;
    Handler initial_handler{&initial_enter_called};
    B.on_enter(S::A,
               [&initial_handler, &href](C& ctx, const S& from, const S& to, const In* input) {
                   initial_handler.on_enter(ctx, from, to, input);
                   href.on_enter(ctx, from, to, input);
               });

    B.from(S::A).on<E>().to(S::B);

    M m = std::move(B).build({});

    assert(initial_enter_called);

    (void)m.update();
    assert(m.context().enter >= 1);
    assert(m.context().do_calls >= 1);

    (void)m.publisher();
}
