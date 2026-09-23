// Copyright (c) 2014, Tom Ritchford <tom@swirly.com>

#pragma once

#include <compare>
#include <concepts>

namespace beast {

/**
 * Zero allows classes to offer efficient comparisons to zero.
 *
 * It's often the case that we have classes which combine a number and a unit.
 * In such cases, comparisons like t > 0 or t != 0 make sense, but comparisons
 * like t > 1 or t != 1 do not. Comparing against kZero expresses exactly that,
 * without constructing a T.
 *
 * A type T participates if either `t.signum()` or an unqualified `signum(t)`
 * found by argument-dependent lookup returns an integer that is negative,
 * zero, or positive according to the sign of t. Both `t == kZero` and
 * `kZero == t` work, as do all six relational operators in either order.
 */
struct Zero
{
    explicit Zero() = default;
};

inline constexpr Zero kZero{};

/**
 * Default implementation of signum: call the member function.
 */
template <class T>
    requires requires(T const& t) {
        { t.signum() } -> std::integral;
    }
[[nodiscard]] constexpr auto
signum(T const& t) noexcept(noexcept(t.signum()))
{
    return t.signum();
}

namespace detail {

/**
 * A type with a usable signum: either the member-based default above, or a
 * `signum(t)` overload in T's own namespace, found by ADL. A user overload
 * that is a better match than the template wins, as usual.
 */
template <class T>
concept HasSignum = requires(T const& t) {
    { signum(t) } -> std::integral;
};

}  // namespace detail

template <detail::HasSignum T>
[[nodiscard]] constexpr bool
operator==(T const& t, Zero) noexcept(noexcept(signum(t)))
{
    return signum(t) == 0;
}

template <detail::HasSignum T>
[[nodiscard]] constexpr std::strong_ordering
operator<=>(T const& t, Zero) noexcept(noexcept(signum(t)))
{
    return signum(t) <=> 0;
}

}  // namespace beast
