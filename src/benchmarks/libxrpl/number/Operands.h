#pragma once

#include <xrpl/basics/Number.h>

#include <benchmarks/libxrpl/number/NumberLike.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <string_view>
#include <utility>
#include <vector>

namespace xrpl::number_bench {

/**
 * Operand count per batch: small enough to stay in L1 for 32-byte types.
 */
constexpr std::size_t kBatchSize = 1024;

/**
 * Raw operand: value = mantissa * 10^exponent.
 */
struct RawOperand
{
    std::int64_t mantissa;
    int exponent;
};

/**
 * Operand distributions. Each one models a class of values the ledger works
 * with, because cost depends on digit count and exponent alignment.
 */
enum class Dataset {
    /**
     * Full-width 19-digit mantissas in [10^18, 2^63-1], exponents in [-10, 10].
     */
    Full,
    /**
     * IOU-like 16-digit mantissas in [10^15, 10^16-1], exponents in [-25, 5].
     */
    Iou,
    /**
     * XRP drop amounts: integers with 1 to 17 digits (log-uniform), exponent 0.
     */
    Drops,
    /**
     * MPT amounts: integers with 1 to 19 digits up to 2^63-1 (log-uniform), exponent 0.
     */
    Mpt,
    /**
     * Rates in [1, 1.01) with full-width mantissas: the operand of interest and amortization
     * math, and of long multiply/divide chains that must not overflow.
     */
    Rate,
    /**
     * Non-integers below 10^18 (19-digit mantissas, exponents in [-18, -1]): rounding to int64.
     */
    Fraction,
    /**
     * Exact halves, m + 0.5 with m < 10^17: the tie cases of rounding to int64.
     */
    Half,
};

constexpr std::string_view
toString(Dataset d)
{
    switch (d)
    {
        case Dataset::Full:
            return "full";
        case Dataset::Iou:
            return "iou";
        case Dataset::Drops:
            return "drops";
        case Dataset::Mpt:
            return "mpt";
        case Dataset::Rate:
            return "rate";
        case Dataset::Fraction:
            return "fraction";
        case Dataset::Half:
            return "half";
    }
    return "unknown";
}

namespace detail {

/**
 * Integer with a log-uniform digit count in [1, maxDigits], capped at `cap`.
 */
inline std::int64_t
logUniformInteger(std::mt19937_64& rng, int maxDigits, std::int64_t cap)
{
    auto const digits = std::uniform_int_distribution<int>{1, maxDigits}(rng);
    auto const lo = static_cast<std::int64_t>(kPowerOfTen[digits - 1]);
    auto const hi =
        digits >= 19 ? cap : std::min(cap, static_cast<std::int64_t>(kPowerOfTen[digits]) - 1);
    return std::uniform_int_distribution<std::int64_t>{lo, hi}(rng);
}

}  // namespace detail

/**
 * Deterministic: the same dataset and seed always produce the same operands.
 */
inline std::vector<RawOperand>
makeRaw(Dataset d, std::uint64_t seed, std::size_t count = kBatchSize)
{
    constexpr auto kMaxInt64 = std::numeric_limits<std::int64_t>::max();
    constexpr auto kE18 = static_cast<std::int64_t>(kPowerOfTen[18]);
    constexpr auto kE16 = static_cast<std::int64_t>(kPowerOfTen[16]);
    constexpr auto kE15 = static_cast<std::int64_t>(kPowerOfTen[15]);

    std::mt19937_64 rng{seed};
    std::vector<RawOperand> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        switch (d)
        {
            case Dataset::Full:
                result.push_back(
                    {std::uniform_int_distribution<std::int64_t>{kE18, kMaxInt64}(rng),
                     std::uniform_int_distribution<int>{-10, 10}(rng)});
                break;
            case Dataset::Iou:
                result.push_back(
                    {std::uniform_int_distribution<std::int64_t>{kE15, kE16 - 1}(rng),
                     std::uniform_int_distribution<int>{-25, 5}(rng)});
                break;
            case Dataset::Drops:
                result.push_back({detail::logUniformInteger(rng, 17, kMaxInt64), 0});
                break;
            case Dataset::Mpt:
                result.push_back({detail::logUniformInteger(rng, 19, kMaxInt64), 0});
                break;
            case Dataset::Rate:
                // [10^18, 1.01 * 10^18) * 10^-18 = [1, 1.01)
                result.push_back(
                    {std::uniform_int_distribution<std::int64_t>{
                         kE18, kE18 + (kE18 / 100) - 1}(rng),
                     -18});
                break;
            case Dataset::Fraction:
                result.push_back(
                    {std::uniform_int_distribution<std::int64_t>{kE18, kMaxInt64}(rng),
                     std::uniform_int_distribution<int>{-18, -1}(rng)});
                break;
            case Dataset::Half:
                result.push_back(
                    {(std::uniform_int_distribution<std::int64_t>{0, (kE18 / 10) - 1}(rng) * 10) +
                         5,
                     -1});
                break;
        }
    }
    return result;
}

template <NumberLike T>
std::vector<T>
materialize(std::vector<RawOperand> const& raw)
{
    std::vector<T> result;
    result.reserve(raw.size());
    for (auto const& r : raw)
        result.push_back(T{r.mantissa, r.exponent});
    return result;
}

/**
 * Pairs of full-width operands whose exponents differ by exactly `gap`, so
 * addition must shift (and round away) `gap` digits of the smaller operand.
 */
inline std::pair<std::vector<RawOperand>, std::vector<RawOperand>>
makeExponentGapPairs(int gap, std::uint64_t seed, std::size_t count = kBatchSize)
{
    auto lhs = makeRaw(Dataset::Full, seed, count);
    auto rhs = makeRaw(Dataset::Full, seed + 1, count);
    for (std::size_t i = 0; i < count; ++i)
        rhs[i].exponent = lhs[i].exponent - gap;
    return {std::move(lhs), std::move(rhs)};
}

/**
 * Pairs of nearly equal full-width operands, so subtraction cancels most
 * leading digits and the result must be renormalized by many places.
 */
inline std::pair<std::vector<RawOperand>, std::vector<RawOperand>>
makeCancellationPairs(std::uint64_t seed, std::size_t count = kBatchSize)
{
    auto lhs = makeRaw(Dataset::Full, seed, count);
    std::vector<RawOperand> rhs;
    rhs.reserve(count);
    std::mt19937_64 rng{seed + 1};
    std::uniform_int_distribution<std::int64_t> delta{1, 1000};
    for (auto const& l : lhs)
        rhs.push_back({l.mantissa - delta(rng), l.exponent});
    return {std::move(lhs), std::move(rhs)};
}

/**
 * Pairs whose exact sum is exactly halfway between two adjacent 19-digit
 * values: a = m * 10^e, b = 5 * 10^(e-1). Exercises round-half-even.
 */
inline std::pair<std::vector<RawOperand>, std::vector<RawOperand>>
makeTiePairs(std::uint64_t seed, std::size_t count = kBatchSize)
{
    auto lhs = makeRaw(Dataset::Full, seed, count);
    std::vector<RawOperand> rhs;
    rhs.reserve(count);
    for (auto const& l : lhs)
        rhs.push_back({5, l.exponent - 1});
    return {std::move(lhs), std::move(rhs)};
}

/**
 * Pairs whose sum straddles 2^63-1 (Number::kMaxRep), where Number's
 * representable values switch from a step of 1 to a step of 10. The addend has
 * one more decimal place than the base, so most sums fall between
 * representable values and must be rounded; sums are within ±200 of the cap,
 * so a fair share land in (2^63-1, 2^63), the interval where LargeLegacy's
 * known cusp-rounding error shows.
 */
inline std::pair<std::vector<RawOperand>, std::vector<RawOperand>>
makeCuspPairs(std::uint64_t seed, std::size_t count = kBatchSize)
{
    constexpr auto kMaxInt64 = std::numeric_limits<std::int64_t>::max();
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<std::int64_t> base{kMaxInt64 - 100, kMaxInt64};
    std::uniform_int_distribution<std::int64_t> addend{1, 2'000};
    std::uniform_int_distribution<int> exponent{-10, 10};
    std::pair<std::vector<RawOperand>, std::vector<RawOperand>> result;
    result.first.reserve(count);
    result.second.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        auto const e = exponent(rng);
        result.first.push_back({base(rng), e});
        result.second.push_back({addend(rng), e - 1});
    }
    return result;
}

}  // namespace xrpl::number_bench
