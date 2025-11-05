#include <cassert>
#include <optional>
#include <variant>

#include <lsm/cosm.hpp>

enum class Node { Idle, Async, Done };
struct Fire {};

using Input = std::variant<Fire>;
using Output = int;

struct Context
{
    int value = 0;
};

using CoMachine = lsm::CoMachine<Node, Input, Output, Context>;

int main()
{
    lsm::co::CoBuilder<CoMachine> builder;
    builder.set_initial(Node::Idle);

    auto& base = builder.base();
    base.on<Fire>(Node::Idle, Node::Async, lsm::create_action<Input, Context>());

    builder.from(Node::Idle)
        .on<Fire>()
        .to(Node::Async)
        .emit([](const Input&, Context& ctx, auto&) -> Output {
            ctx.value = 11;
            return 11;
        })
        .attach();

    auto bundle = std::move(builder).build(Context{}, nullptr);

    assert(bundle.machine.state() == Node::Idle);

    return 0;
}
