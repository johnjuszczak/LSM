#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <lsm/core.hpp>

enum class S { Idle, Busy };
struct Start
{
};
struct Stop
{
};
using Input = std::variant<Start, Stop>;
using Output = std::monostate;
struct Ctx
{
};

using PubQueue = lsm::publisher::Queue<std::vector<std::string>>;

struct PubHandler
{
    void on_do(Ctx&, const S&, PubQueue& pub)
    {
        pub.publish(std::string{"[pub] on_do"});
    }
};

int main()
{
    using M = lsm::Machine<S, Input, Output, Ctx, lsm::policy::copy, lsm::policy::Publisher<PubQueue>>;
    M::Builder B;
    B.set_initial(S::Idle);

    std::vector<std::string> buffer;
    B.set_publisher(PubQueue{buffer});

    // Bind by_shared
    auto hp = std::make_shared<PubHandler>();
    B.on_state(S::Idle, hp, lsm::bind::by_shared{});

    B.from(S::Idle).on<Start>().to(S::Busy);
    B.from(S::Busy).on<Stop>().to(S::Idle);

    M m = std::move(B).build({});
    (void)m.update();
    // ensure publisher saw messages
    for(const auto& s : buffer)
    {
        std::cout << s << "\n";
    }
}

