#include <cassert>
#include <coroutine>
#include <optional>
#include <variant>
#include <vector>

#include <lsm/core.hpp>
#include <lsm/cosm.hpp>

enum class State { Idle, Done };
struct Emit { int value; };
using Input = std::variant<Emit>;
using Output = int;
struct Context {
    int total = 0;
};

using Publisher = lsm::publisher::Queue<std::vector<int>>;
using Machine = lsm::Machine<State, Input, Output, Context, lsm::policy::copy, lsm::policy::Publisher<Publisher>>;

int main() {
    std::vector<int> events;

    {
        Publisher publisher{events};
        Machine::Builder builder;
        builder.set_initial(State::Idle);
        builder.set_publisher(publisher);
        builder.on<Emit>(State::Idle, State::Done,
                         [](const Emit& evt, Context& ctx, Publisher& pub) {
                             ctx.total += evt.value;
                             pub.publish(evt.value);
                         });

        Machine machine = std::move(builder).build({});
        machine.dispatch(Input{Emit{5}});
        assert(machine.context().total == 5);
        assert(events.size() == 1 && events[0] == 5);
    }

    events.clear();

    Publisher publisher_async{events};
    Machine::Builder builder_async;
    builder_async.set_initial(State::Idle);
    builder_async.set_publisher(publisher_async);
    builder_async.on<Emit>(State::Idle, State::Done,
                           [](const Emit&, Context&, Publisher& pub) {
                               pub.publish(1);
                           });

    Machine machine_async = std::move(builder_async).build({});
    lsm::co::Adapter<Machine> adapter(machine_async);
    adapter.bind_async(State::Idle, State::Done,
        [](const Input&, Context&, lsm::co::CancelToken, Publisher& pub) -> lsm::co::Task<std::optional<Output>> {
            pub.publish(42);
            co_return std::nullopt;
        }
    );

    auto task = adapter.dispatch_async(Input{Emit{0}});
    while (!task.await_ready()) {
        task.await_suspend(std::noop_coroutine());
    }
    auto out = task.await_resume();
    assert(!out);
    assert(events.size() == 2 && events[0] == 1 && events[1] == 42);

    return 0;
}
