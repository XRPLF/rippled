#pragma once

#include <xrpl/basics/Number.h>

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>

namespace xrpl::number_bench {

/**
 * Sign, coefficient, and exponent of a finite decimal value:
 * value = (negative ? -1 : 1) * coefficient * 10^exponent.
 *
 * The coefficient is 128 bits wide so the same struct can describe 34- and
 * 38-digit types. Different libraries normalize to different precisions, so
 * compare decompositions with `canonical()`, which strips trailing zeros.
 */
struct Decomposed
{
    bool negative = false;
    unsigned __int128 coefficient = 0;
    int exponent = 0;

    [[nodiscard]] constexpr Decomposed
    canonical() const noexcept
    {
        if (coefficient == 0)
            return {};
        Decomposed result = *this;
        while (result.coefficient % 10 == 0)
        {
            result.coefficient /= 10;
            ++result.exponent;
        }
        return result;
    }

    friend constexpr bool
    operator==(Decomposed const&, Decomposed const&) = default;
};

[[nodiscard]] inline Decomposed
decompose(Number const& x) noexcept
{
    auto const m = x.mantissa();
    // Negating INT64_MIN is UB, but Number never produces it (see Number.h).
    auto const magnitude = static_cast<std::uint64_t>(m < 0 ? -m : m);
    return {.negative = m < 0, .coefficient = magnitude, .exponent = x.exponent()};
}

/**
 * The subset of Number's interface that ledger code relies on. Benchmarks and
 * the divergence harness are written against this concept, so any type
 * satisfying it, including a future 128-bit Number, can be dropped in.
 */
template <class T>
concept NumberLike = std::copyable<T> && std::totally_ordered<T> &&
    requires(T a, T b, std::int64_t mantissa, int exponent, unsigned n, Number::RoundingMode mode) {
        T{mantissa};
        T{mantissa, exponent};
        { a + b } -> std::same_as<T>;
        { a - b } -> std::same_as<T>;
        { a * b } -> std::same_as<T>;
        { a / b } -> std::same_as<T>;
        { -a } -> std::same_as<T>;
        { a += b } -> std::same_as<T&>;
        { a -= b } -> std::same_as<T&>;
        { a *= b } -> std::same_as<T&>;
        { a /= b } -> std::same_as<T&>;
        static_cast<std::int64_t>(a);
        { to_string(a) } -> std::same_as<std::string>;
        { abs(a) } -> std::same_as<T>;
        { power(a, n) } -> std::same_as<T>;
        { root2(a) } -> std::same_as<T>;
        { T::setround(mode) } -> std::same_as<Number::RoundingMode>;
        { T::getround() } -> std::same_as<Number::RoundingMode>;
    };

/**
 * A NumberLike type whose exact value can be inspected digit by digit.
 */
template <class T>
concept DecomposableNumber = NumberLike<T> && requires(T a) {
    { decompose(a) } -> std::same_as<Decomposed>;
};

static_assert(DecomposableNumber<Number>);

}  // namespace xrpl::number_bench
