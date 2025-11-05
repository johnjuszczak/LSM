#include <coroutine>
#include <iostream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <lsm/core.hpp>
#include <lsm/cosm.hpp>

enum class State { Idle, Active, Done };
struct Start {};

using Input  = std::variant<Start>;
using Output = std::string;
struct Context {
    int completed = 0;
};

using Publisher = lsm::publisher::Queue<std::vector<std::string>>;
using Machine = lsm::Machine<State, Input, Output, Context, lsm::policy::copy, lsm::policy::Publisher<Publisher>>;

int main() {
    std::vector<std::string> events;
    Publisher publisher{events};

    Machine::Builder builder;
    builder.set_initial(State::Idle);
    builder.set_publisher(publisher);
    builder.on<Start>(State::Idle, State::Active,
                      [](const Start&, Context&, Publisher& pub) {
                          pub.publish("start-event");
                      });

    Machine machine = std::move(builder).build({});

    lsm::co::Adapter<Machine> adapter(machine);
    adapter.bind_async(State::Active, State::Done,
        [](const Input&, Context& ctx, lsm::co::CancelToken, Publisher& pub) -> lsm::co::Task<std::optional<Output>> {
            pub.publish("async-inflight");
            ctx.completed = 1;
            co_return std::optional<Output>{"result"};
        }
    );

    auto task = adapter.dispatch_async(Input{Start{}});
    while (!task.await_ready()) {
        task.await_suspend(std::noop_coroutine());
    }
    auto out = task.await_resume();
    if (out) {
        events.push_back(*out);
    }

    std::cout << "Logged:";
    for (const auto& entry : events) {
        std::cout << ' ' << entry;
    }
    std::cout << "\ncompleted=" << machine.context().completed << "\n";

    return 0;
}

