#include <iostream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <lsm/core.hpp>

enum class State { Idle, Active };
struct Add { int value; };
struct Flush {};

using Input  = std::variant<Add, Flush>;
using Output = int;
using Context = std::monostate;

using Publisher = lsm::publisher::Queue<std::vector<int>>;
using Machine = lsm::Machine<State,
                              Input,
                              Output,
                              Context,
                              lsm::policy::copy,
                              lsm::policy::Publisher<Publisher>>;

int main() {
    std::vector<int> published;
    Publisher queue{published};

    Machine::Builder builder;
    builder.set_initial(State::Idle);
    builder.set_publisher(queue);

    builder.on<Add>(State::Idle, State::Idle,
                    [](const Add& evt, Context&, Publisher& publisher) {
                        publisher.publish(evt.value);
                    });

    builder.on<Flush>(State::Idle, State::Idle,
                      [](const Flush&, Context&, Publisher& publisher) {
                          publisher.publish(0);
                      });

    Machine machine = std::move(builder).build({});
    machine.dispatch(Input{Add{1}});
    machine.dispatch(Input{Add{2}});
    machine.dispatch(Input{Flush{}});

    std::cout << "Published values:";
    for (int value : published) {
        std::cout << ' ' << value;
    }
    std::cout << "\n";

    return 0;
}

