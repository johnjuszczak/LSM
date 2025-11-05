#include <iostream>
#include <optional>
#include <string>
#include <variant>

#include <lsm/core.hpp>

enum class S { A, B };
struct E1
{
};
struct E2
{
};
using Input = std::variant<E1, E2>;
using Output = std::string;
struct Ctx
{
    int ticks = 0;
};

struct Handler
{
    void on_enter(Ctx&, const S&, const S&, const Input*)
    {
        std::cout << "[handler] on_enter\n";
    }
    std::optional<Output> on_do(Ctx&, const S&)
    {
        return std::optional<Output>{"[handler] on_do\n"};
    }
    void on_exit(Ctx&, const S&, const S&, const Input*)
    {
        std::cout << "[handler] on_exit\n";
    }
};

int main()
{
    using M = lsm::Machine<S, Input, Output, Ctx>;
    M::Builder B;
    B.set_initial(S::A);

    // Bind by_ref
    Handler h;
    B.on_state(S::A, h, lsm::bind::by_ref{});

    // Simple transition
    B.from(S::A).on<E1>().to(S::B);

    M m = std::move(B).build({});
    (void)m.dispatch(Input{E1{}});
    (void)m.update();
}

