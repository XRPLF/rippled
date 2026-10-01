#pragma once

// Ledger formulas, written once against NumberLike so every library runs the
// identical computation. Each kernel mirrors a production function; keep them
// in sync if those change:
//
//   ammSwapIn / ammSwapOut  swapAssetIn / swapAssetOut, fixAMMv1_1 branch
//                           (include/xrpl/ledger/helpers/AMMHelpers.h)
//   vaultAssetsToShares     assetsToSharesDeposit
//   vaultSharesToAssets     sharesToAssetsDeposit
//                           (src/libxrpl/ledger/helpers/VaultHelpers.cpp)
//   loanPeriodicRate        loanPeriodicRate
//   loanPowerMinusOne       computePowerMinusOneHybrid
//   loanPeriodicPayment     loanPeriodicPayment, fixCleanup3_2_0 branch
//                           (src/libxrpl/ledger/helpers/LendingHelpers.cpp)
//
// loanAmortize is not a production function: it runs a full payment schedule
// (interest = balance * rate; balance -= payment - interest) so error
// accumulates the way it does over a loan's life. The exact remaining balance
// after the last payment is 0.
//
// The kernels operate on bare values, not STAmount or ledger entries, and
// omit the final rounding to the asset's scale, so they measure arithmetic
// alone.

#include <xrpl/basics/Number.h>

#include <benchmarks/libxrpl/number/NumberLike.h>

#include <cstdint>

namespace xrpl::number_bench {

namespace detail {

template <NumberLike T>
T
integer(std::int64_t value)
{
    return T{value};
}

/**
 * Sets a rounding mode for its lifetime, then restores the previous one.
 */
template <NumberLike T>
class RoundGuard
{
    Number::RoundingMode saved_;

public:
    explicit RoundGuard(Number::RoundingMode mode) : saved_{T::setround(mode)}
    {
    }

    ~RoundGuard()
    {
        T::setround(saved_);
    }

    RoundGuard(RoundGuard const&) = delete;
    RoundGuard&
    operator=(RoundGuard const&) = delete;
};

}  // namespace detail

/**
 * Amount of the out asset received for swapping `assetIn` into the pool.
 * Rounding at each step favors the AMM.
 */
template <NumberLike T>
T
ammSwapIn(T const& poolIn, T const& poolOut, T const& assetIn, std::uint16_t tfee)
{
    using enum Number::RoundingMode;
    auto const saved = T::getround();

    T::setround(Upward);
    auto const numerator = poolIn * poolOut;
    auto const fee = detail::integer<T>(tfee) / detail::integer<T>(100'000);

    T::setround(Downward);
    auto const denom = poolIn + (assetIn * (detail::integer<T>(1) - fee));

    T::setround(Upward);
    auto const ratio = numerator / denom;

    T::setround(Downward);
    auto const swapOut = poolOut - ratio;

    T::setround(saved);
    return swapOut;
}

/**
 * Amount of the in asset required to take `assetOut` out of the pool.
 * Rounding at each step favors the AMM.
 */
template <NumberLike T>
T
ammSwapOut(T const& poolIn, T const& poolOut, T const& assetOut, std::uint16_t tfee)
{
    using enum Number::RoundingMode;
    auto const saved = T::getround();

    T::setround(Upward);
    auto const numerator = poolIn * poolOut;

    T::setround(Downward);
    auto const denom = poolOut - assetOut;

    T::setround(Upward);
    auto const ratio = numerator / denom;
    auto const numerator2 = ratio - poolIn;
    auto const fee = detail::integer<T>(tfee) / detail::integer<T>(100'000);

    T::setround(Downward);
    auto const feeMult = detail::integer<T>(1) - fee;

    T::setround(Upward);
    auto const swapIn = numerator2 / feeMult;

    T::setround(saved);
    return swapIn;
}

/**
 * Shares minted for depositing `assets` into a non-empty vault, truncated to
 * an integer (vault shares are MPTs).
 */
template <NumberLike T>
std::int64_t
vaultAssetsToShares(T const& shareTotal, T const& assetTotal, T const& assets)
{
    auto const shares = (shareTotal * assets) / assetTotal;
    detail::RoundGuard<T> const guard{Number::RoundingMode::TowardsZero};
    return static_cast<std::int64_t>(shares);
}

/**
 * Assets corresponding to `shares` of a non-empty vault.
 */
template <NumberLike T>
T
vaultSharesToAssets(T const& assetTotal, T const& shareTotal, T const& shares)
{
    return (assetTotal * shares) / shareTotal;
}

constexpr std::uint32_t kSecondsInYear = 365 * 24 * 60 * 60;

/**
 * Interest rate per payment interval, from an annual rate in 1/10 bips.
 */
template <NumberLike T>
T
loanPeriodicRate(std::uint32_t interestRateTenthBips, std::uint32_t paymentInterval)
{
    auto const interval = detail::integer<T>(paymentInterval);
    return interval * detail::integer<T>(interestRateTenthBips) / detail::integer<T>(100'000) /
        detail::integer<T>(kSecondsInYear);
}

/**
 * (1 + r)^n - 1: closed form, or a binomial series when r * n is tiny and the
 * closed form would cancel catastrophically.
 */
template <NumberLike T>
T
loanPowerMinusOne(T const& rate, std::uint32_t payments)
{
    auto const n = detail::integer<T>(payments);
    if (n * rate >= T{1, -9})
        return power(detail::integer<T>(1) + rate, payments) - detail::integer<T>(1);

    T term = n * rate;
    T sum = term;
    for (std::uint32_t k = 1; k < payments; ++k)
    {
        term = term * rate * detail::integer<T>(payments - k) / detail::integer<T>(k + 1);
        T const next = sum + term;
        if (next == sum)
            break;
        sum = next;
    }
    return sum;
}

/**
 * Level payment that amortizes `principal` over `payments` periods.
 */
template <NumberLike T>
T
loanPeriodicPayment(T const& principal, T const& rate, std::uint32_t payments)
{
    auto const raisedMinusOne = loanPowerMinusOne(rate, payments);
    auto const raised = detail::integer<T>(1) + raisedMinusOne;
    return principal * ((rate * raised) / raisedMinusOne);
}

/**
 * Remaining balance after making every scheduled payment. Exactly 0 in exact
 * arithmetic; anything else is accumulated rounding error.
 */
template <NumberLike T>
T
loanAmortize(T const& principal, T const& rate, std::uint32_t payments)
{
    auto const payment = loanPeriodicPayment(principal, rate, payments);
    T balance = principal;
    for (std::uint32_t i = 0; i < payments; ++i)
    {
        auto const interest = balance * rate;
        balance -= payment - interest;
    }
    return balance;
}

}  // namespace xrpl::number_bench
