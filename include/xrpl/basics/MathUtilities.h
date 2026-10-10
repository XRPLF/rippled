#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace xrpl {

/**
 * Add two signed 64-bit integers, returning std::nullopt when the exact
 * mathematical sum is not representable in std::int64_t.
 */
[[nodiscard]] constexpr std::optional<std::int64_t>
checkedAdd(std::int64_t a, std::int64_t b) noexcept
{
    using L = std::numeric_limits<std::int64_t>;

    if ((b > 0 && a > L::max() - b) || (b < 0 && a < L::min() - b))
        return std::nullopt;

    return a + b;
}

/**
 * Subtract two signed 64-bit integers, returning std::nullopt when the exact
 * mathematical difference is not representable in std::int64_t.
 */
[[nodiscard]] constexpr std::optional<std::int64_t>
checkedSub(std::int64_t a, std::int64_t b) noexcept
{
    using L = std::numeric_limits<std::int64_t>;

    if ((b > 0 && a < L::min() + b) || (b < 0 && a > L::max() + b))
        return std::nullopt;

    return a - b;
}

/**
 * Multiply two signed 64-bit integers, returning std::nullopt when the exact
 * mathematical product is not representable in std::int64_t.
 */
[[nodiscard]] constexpr std::optional<std::int64_t>
checkedMul(std::int64_t a, std::int64_t b) noexcept
{
    using L = std::numeric_limits<std::int64_t>;

    if (a == 0 || b == 0)
        return 0;

    if (a > 0)
    {
        if (b > 0 ? a > L::max() / b : b < L::min() / a)
            return std::nullopt;
    }
    else if (b > 0 ? a < L::min() / b : b < L::max() / a)
    {
        return std::nullopt;
    }

    return a * b;
}

static_assert(checkedAdd(0, 0) == 0);
static_assert(checkedAdd(1, -1) == 0);
static_assert(checkedAdd(-5, 2) == -3);
static_assert(!checkedAdd(std::numeric_limits<std::int64_t>::max(), 1).has_value());
static_assert(!checkedAdd(std::numeric_limits<std::int64_t>::min(), -1).has_value());
static_assert(
    checkedAdd(std::numeric_limits<std::int64_t>::max() - 1, 1) ==
    std::numeric_limits<std::int64_t>::max());
static_assert(
    checkedAdd(
        std::numeric_limits<std::int64_t>::min(),
        std::numeric_limits<std::int64_t>::max()) == -1);
static_assert(
    checkedAdd(
        std::numeric_limits<std::int64_t>::max(),
        std::numeric_limits<std::int64_t>::min()) == -1);
static_assert(
    !checkedAdd(std::numeric_limits<std::int64_t>::max(), std::numeric_limits<std::int64_t>::max())
         .has_value());
static_assert(
    !checkedAdd(std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::min())
         .has_value());

static_assert(checkedSub(0, 0) == 0);
static_assert(checkedSub(1, 1) == 0);
static_assert(checkedSub(-5, 2) == -7);
static_assert(checkedSub(-5, -2) == -3);
static_assert(!checkedSub(std::numeric_limits<std::int64_t>::min(), 1).has_value());
static_assert(!checkedSub(std::numeric_limits<std::int64_t>::max(), -1).has_value());
static_assert(
    checkedSub(std::numeric_limits<std::int64_t>::min() + 1, 1) ==
    std::numeric_limits<std::int64_t>::min());
static_assert(
    checkedSub(-1, std::numeric_limits<std::int64_t>::max()) ==
    std::numeric_limits<std::int64_t>::min());
static_assert(
    !checkedSub(std::numeric_limits<std::int64_t>::max(), std::numeric_limits<std::int64_t>::min())
         .has_value());
static_assert(
    !checkedSub(std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max())
         .has_value());

/**
 * Calculate one number divided by another number in percentage.
 * The result is rounded up to the next integer, and capped in the range [0,100]
 * E.g. calculatePercent(1, 100) = 1 because 1/100 = 0.010000
 *      calculatePercent(1, 99) = 2 because 1/99 = 0.010101
 *      calculatePercent(0, 100) = 0
 *      calculatePercent(100, 100) = 100
 *      calculatePercent(200, 100) = 100 because the result is capped to 100
 *
 * @param count  dividend
 * @param total  divisor
 * @return the percentage, in [0, 100]
 *
 * @note total cannot be zero.
 */
constexpr std::size_t
calculatePercent(std::size_t count, std::size_t total)
{
    assert(total != 0);  // NOTE No XRPL_ASSERT here, because constexpr
    return ((std::min(count, total) * 100) + total - 1) / total;
}

// unit tests
static_assert(calculatePercent(1, 2) == 50);
static_assert(calculatePercent(0, 100) == 0);
static_assert(calculatePercent(100, 100) == 100);
static_assert(calculatePercent(200, 100) == 100);
static_assert(calculatePercent(1, 100) == 1);
static_assert(calculatePercent(1, 99) == 2);
static_assert(calculatePercent(6, 14) == 43);
static_assert(calculatePercent(29, 33) == 88);
static_assert(calculatePercent(1, 64) == 2);
static_assert(calculatePercent(0, 100'000'000) == 0);
static_assert(calculatePercent(1, 100'000'000) == 1);
static_assert(calculatePercent(50'000'000, 100'000'000) == 50);
static_assert(calculatePercent(50'000'001, 100'000'000) == 51);
static_assert(calculatePercent(99'999'999, 100'000'000) == 100);

}  // namespace xrpl
