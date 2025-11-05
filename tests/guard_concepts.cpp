#include <optional>
#include <variant>

#include <lsm/core.hpp>

enum class State { A, B };
struct Event {};
using Input = std::variant<Event>;
using Output = int;

using Machine = lsm::Machine<State, Input, Output, std::monostate>;

struct BadAction {
    std::optional<Output> operator()(int) const { return std::optional<Output>{}; }
};

struct BadGuard {
    bool operator()(int) const { return true; }
};

struct GoodGuard {
    bool operator()(const Input& in, const std::monostate&) const {
        return std::holds_alternative<Event>(in);
    }
};

static_assert(!lsm::ActionFor<BadAction, Event, std::monostate, Output>);
static_assert(!lsm::GuardFor<BadGuard, Input, std::monostate>);
static_assert(lsm::GuardFor<GoodGuard, Input, std::monostate>);

int main() {
    return 0;
}
