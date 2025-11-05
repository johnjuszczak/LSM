#ifndef LSM_DETAIL_CONCEPTS_HPP
#define LSM_DETAIL_CONCEPTS_HPP

#include <concepts>
#include <optional>
#include <type_traits>
#include <variant>

#include <lsm/detail/policy.hpp>

namespace lsm
{

namespace detail
{
template <class>
struct is_std_variant : std::false_type
{
};
template <class... Ts>
struct is_std_variant<std::variant<Ts...>> : std::true_type
{
};
template <class T>
constexpr bool is_std_variant_v = is_std_variant<std::remove_cvref_t<T>>::value;
} // namespace detail

template <class T>
concept IsVariant = detail::is_std_variant_v<T>;

template <class T>
concept EqComparable = std::equality_comparable<std::remove_cvref_t<T>>;

template <class T>
inline constexpr std::type_identity<T> type_c{};

template <class F, class In, class Ctx, class Out>
concept ActionFor =
    requires(F f, const In& in, Ctx& ctx) {
        { f(in, ctx) } -> std::convertible_to<std::optional<Out>>;
    };

template <class F, class In, class Ctx>
concept GuardFor =
    requires(F f, const In& in, const Ctx& ctx) {
        { f(in, ctx) } -> std::convertible_to<bool>;
    };

template<class T>
concept PolicyHasCallableTemplate = requires {
    typename T::template Callable<void()>;
};

// First-Class State Handler detection concepts
// Optional member methods accepted; used to constrain object-centric builder overloads.
template <class T, class State, class Input, class Output, class Ctx>
concept has_on_enter = requires(T t, Ctx& ctx, const State& from, const State& to, const Input* in) {
    { t.on_enter(ctx, from, to, in) } -> std::same_as<void>;
};

template <class T, class State, class Input, class Output, class Ctx>
concept has_on_exit = requires(T t, Ctx& ctx, const State& from, const State& to, const Input* in) {
    { t.on_exit(ctx, from, to, in) } -> std::same_as<void>;
};

template <class T, class State, class Output, class Ctx>
concept has_on_do_return = requires(T t, Ctx& ctx, const State& st) {
    { t.on_do(ctx, st) } -> std::convertible_to<std::optional<Output>>;
};

template <class T, class State, class Pub, class Ctx>
concept has_on_do_publish = requires(T t, Ctx& ctx, const State& st, Pub& pub) {
    { t.on_do(ctx, st, pub) } -> std::same_as<void>;
};

template <class T>
concept has_on_do_member = requires {
    &T::on_do;
};

namespace detail
{
template <class E>
struct is_publisher_effect : std::false_type
{
};
template <class P>
struct is_publisher_effect<policy::Publisher<P>> : std::true_type
{
    using publisher_type = P;
};
} // namespace detail

template <class T, class State, class Input, class Output, class Ctx, class EffectPolicy>
concept StateHandlerFor =
    // ReturnOutput policy variant
    (std::is_same_v<EffectPolicy, policy::ReturnOutput<Output>> &&
     (has_on_enter<T, State, Input, Output, Ctx> || has_on_exit<T, State, Input, Output, Ctx> ||
      has_on_do_return<T, State, Output, Ctx>)) ||
    // Publisher policy variant
    (detail::is_publisher_effect<EffectPolicy>::value &&
     (has_on_enter<T, State, Input, Output, Ctx> || has_on_exit<T, State, Input, Output, Ctx> ||
      has_on_do_publish<T, State, typename detail::is_publisher_effect<EffectPolicy>::publisher_type, Ctx>));

} // namespace lsm

#endif
