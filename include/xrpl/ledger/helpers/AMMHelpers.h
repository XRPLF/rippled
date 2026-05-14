/** @file
 *  Mathematical and operational backbone of the XRPL Automated Market Maker.
 *
 *  Provides every computation needed to run a constant-product AMM pool:
 *  LP token minting and burning (XLS-30d Equations 3, 4, 7, 8), spot-price
 *  quality alignment against the central limit order book, swap execution with
 *  rigorous directional rounding, and ledger-state helpers for pool balance
 *  queries and AMM account lifecycle management.
 *
 *  All arithmetic observes the pool invariant:
 *  @code
 *    sqrt(poolAsset1 × poolAsset2) >= LPTokenBalance
 *  @endcode
 *  Rounding is always directed to keep the pool at least as large as required.
 *  The `fixAMMv1_1` amendment introduced per-step directional rounding for
 *  swaps; `fixAMMv1_3` extended this discipline to LP token and
 *  deposit/withdrawal formulas.  Pre-amendment paths are preserved for
 *  historic ledger replay.
 */
#pragma once

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>

namespace xrpl {

namespace detail {

/** Scale @p amount down by 99.99% as a last-resort quality rescue.
 *
 *  When the rounded offer from `getAMMOfferStartWithTakerGets` or
 *  `getAMMOfferStartWithTakerPays` still falls below the target quality due
 *  to XRP integer-drop discretization, this function shrinks it by 0.01%
 *  (rounding toward zero) so the resulting offer quality meets or exceeds
 *  the target without generating an implausibly small trade.
 *
 *  @param amount The offer side (takerGets or takerPays) to reduce.
 *  @return The reduced amount, or zero if already at zero.
 */
Number
reduceOffer(auto const& amount)
{
    static Number const kREDUCED_OFFER_PCT(9999, -4);

    // Make sure the result is always less than amount or zero.
    NumberRoundModeGuard const mg(Number::RoundingMode::TowardsZero);
    return amount * kREDUCED_OFFER_PCT;
}

}  // namespace detail

/** Direction tag used throughout deposit/withdrawal and rounding helpers.
 *
 *  Passed to functions that behave asymmetrically between deposit (LP tokens
 *  rounded down, assets rounded up) and withdrawal (LP tokens rounded up,
 *  assets rounded down) to preserve the pool invariant.
 */
enum class IsDeposit : bool { No = false, Yes = true };

/** Compute the initial LP token supply for a newly seeded AMM pool.
 *
 *  Uses the geometric mean `sqrt(asset1 × asset2)`, which sets the
 *  pool invariant to equality at creation: `sqrt(asset1 × asset2) == LPTokens`.
 *  Under `fixAMMv1_3` the result is rounded downward so the pool starts
 *  with a slight surplus, preserving the invariant.
 *
 *  @param asset1 Balance of the first pool asset.
 *  @param asset2 Balance of the second pool asset.
 *  @param lptIssue Asset descriptor identifying the LP token currency/issuer.
 *  @return Initial LP token amount as an IOU `STAmount`.
 */
STAmount
ammLPTokens(STAmount const& asset1, STAmount const& asset2, Asset const& lptIssue);

/** LP tokens minted for a single-asset deposit (XLS-30d Equation 3).
 *
 *  A single-sided deposit is economically equivalent to a proportional
 *  deposit plus a fee-bearing swap; the fee is embedded via `feeMult` and
 *  `feeMultHalf`.  Under `fixAMMv1_3` the final multiplication is rounded
 *  downward so fewer tokens are issued, preserving the pool invariant.
 *
 *  @param asset1Balance Current pool balance of the asset being deposited.
 *  @param asset1Deposit Amount being deposited.
 *  @param lptAMMBalance Current total LP token supply.
 *  @param tfee Trading fee in basis points (e.g. 1000 = 1%).
 *  @return LP tokens to mint for the depositor.
 */
STAmount
lpTokensOut(
    STAmount const& asset1Balance,
    STAmount const& asset1Deposit,
    STAmount const& lptAMMBalance,
    std::uint16_t tfee);

/** Asset deposit required to receive a given number of LP tokens (XLS-30d Equation 4).
 *
 *  Inverse of `lpTokensOut`: solves Equation 3 for the deposit amount given a
 *  desired token output.  The solution is a quadratic whose positive root is
 *  found via `solveQuadraticEq`.  Under `fixAMMv1_3` the result is rounded
 *  upward so the depositor contributes slightly more, preserving the pool
 *  invariant.
 *
 *  @param asset1Balance Current pool balance of the asset to deposit.
 *  @param lptAMMBalance Current total LP token supply.
 *  @param lpTokens Desired LP token amount.
 *  @param tfee Trading fee in basis points.
 *  @return Asset amount the depositor must contribute.
 */
STAmount
ammAssetIn(
    STAmount const& asset1Balance,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens,
    std::uint16_t tfee);

/** LP tokens to burn for a single-asset withdrawal (XLS-30d Equation 7).
 *
 *  Computes how many LP tokens must be redeemed to withdraw a specified asset
 *  amount.  Returns zero if the inputs make calculation impossible.  Under
 *  `fixAMMv1_3` the final multiplication is rounded upward so more tokens must
 *  be burned, preserving the pool invariant.
 *
 *  @param asset1Balance Current pool balance of the asset being withdrawn.
 *  @param asset1Withdraw Requested withdrawal amount.
 *  @param lptAMMBalance Current total LP token supply.
 *  @param tfee Trading fee in basis points.
 *  @return LP tokens the withdrawer must burn, or zero if the calculation fails.
 */
STAmount
lpTokensIn(
    STAmount const& asset1Balance,
    STAmount const& asset1Withdraw,
    STAmount const& lptAMMBalance,
    std::uint16_t tfee);

/** Asset returned when burning a given number of LP tokens (XLS-30d Equation 8).
 *
 *  Inverse of `lpTokensIn`: solves Equation 7 for the withdrawal amount given
 *  the token burn.  Under `fixAMMv1_3` the final multiplication is rounded
 *  downward so the withdrawer receives slightly less, preserving the pool
 *  invariant.
 *
 *  @param assetBalance Current pool balance of the asset to withdraw.
 *  @param lptAMMBalance Current total LP token supply.
 *  @param lpTokens LP tokens being burned.
 *  @param tfee Trading fee in basis points.
 *  @return Asset amount returned to the withdrawer.
 */
STAmount
ammAssetOut(
    STAmount const& assetBalance,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens,
    std::uint16_t tfee);

/** Check whether two `Quality` values are within a relative tolerance.
 *
 *  `Quality` has no subtraction operator, so the comparison is performed via
 *  `Quality::rate()`, which returns the *inverse* of quality (output/input).
 *  The formula `(min.rate - max.rate) / min.rate < dist` is equivalent to
 *  the standard `(max - min) / max < dist` after accounting for the inversion.
 *  Used in `changeSpotPriceQuality` to suppress trace-level errors when the
 *  quality mismatch is within one part in ten million (1e-7).
 *
 *  @param calcQuality Computed quality.
 *  @param reqQuality Target quality.
 *  @param dist Maximum acceptable relative distance (e.g. `Number(1, -7)`).
 *  @return `true` if the two qualities are within @p dist of each other.
 */
inline bool
withinRelativeDistance(Quality const& calcQuality, Quality const& reqQuality, Number const& dist)
{
    if (calcQuality == reqQuality)
        return true;
    auto const [min, max] = std::minmax(calcQuality, reqQuality);
    // Relative distance is (max - min)/max. Can't use basic operations
    // on Quality. Have to use Quality::rate() instead, which
    // is inverse of quality: (1/max.rate - 1/min.rate)/(1/max.rate)
    return ((min.rate() - max.rate()) / min.rate()) < dist;
}

/** Check whether two numeric amounts are within a relative tolerance.
 *
 *  Computes `(max - min) / max` and tests that it is less than @p dist.
 *  Accepted for `STAmount`, `IOUAmount`, `XRPAmount`, `MPTAmount`, and
 *  `Number`.  Used alongside the `Quality` overload to emit quality-mismatch
 *  errors only when the discrepancy is truly significant.
 *
 *  @tparam Amt Amount type; constrained to the five types listed above.
 *  @param calc Computed amount.
 *  @param req Target amount.
 *  @param dist Maximum acceptable relative distance.
 *  @return `true` if the two amounts are within @p dist of each other.
 */
template <typename Amt>
    requires(
        std::is_same_v<Amt, STAmount> || std::is_same_v<Amt, IOUAmount> ||
        std::is_same_v<Amt, XRPAmount> || std::is_same_v<Amt, MPTAmount> ||
        std::is_same_v<Amt, Number>)
bool
withinRelativeDistance(Amt const& calc, Amt const& req, Number const& dist)
{
    if (calc == req)
        return true;
    auto const [min, max] = std::minmax(calc, req);
    return ((max - min) / max) < dist;
}

/** Smallest positive root of `a·x² + b·x + c = 0`, used to minimize offer size.
 *
 *  Uses the numerically stable "citardauq" formula (Blinn 2006): when `b > 0`
 *  it computes `2c / (-b - sqrt(d))` instead of the standard
 *  `(-b + sqrt(d)) / 2a`, avoiding catastrophic cancellation when the two
 *  terms in the numerator are nearly equal.  Minimizing the root maximizes
 *  offer quality in `getAMMOfferStartWithTakerGets` / `getAMMOfferStartWithTakerPays`.
 *
 *  @param a Quadratic coefficient.
 *  @param b Linear coefficient.
 *  @param c Constant term.
 *  @return The smallest positive root, or `std::nullopt` if the discriminant
 *      is negative (no real solution) or the root is non-positive.
 */
std::optional<Number>
solveQuadraticEqSmallest(Number const& a, Number const& b, Number const& c);

/** Generate a synthetic AMM offer whose quality matches @p targetQuality,
 *  starting from takerGets (XRP out, IOU in).
 *
 *  Used when the pool pays XRP (IOU-in / XRP-out).  Starting from the XRP
 *  side ensures that rounding XRP down to integer drops improves rather than
 *  degrades offer quality (post-`fixAMMv1_1` behavior).
 *
 *  Two binding constraints are solved and the smaller takerGets is chosen:
 *  - Scenario A — post-swap spot price equals @p targetQuality:
 *    `o² + o·(I·Qt·(1 - 1/f) - 2·O) + O² - Qt·I·O = 0`
 *  - Scenario B — effective offer price equals @p targetQuality:
 *    `o = O - I·Qt / f`
 *
 *  where `O = poolPays`, `I = poolGets`, `f = feeMult(tfee)`.
 *  takerPays is then derived from the swap-out equation.  If the resulting
 *  offer quality is still below @p targetQuality after rounding, a 99.99%
 *  rescale via `detail::reduceOffer` is attempted.
 *
 *  @tparam TIn  Asset type flowing into the pool (IOU side).
 *  @tparam TOut Asset type flowing out of the pool (XRP side).
 *  @param pool Current AMM pool balances (`in` = poolGets, `out` = poolPays).
 *  @param targetQuality Desired offer quality (CLOB best quality).
 *  @param tfee Trading fee in basis points.
 *  @return Seated `{takerPays, takerGets}` amounts, or `std::nullopt` if a
 *      valid offer cannot be generated (e.g. target quality unreachable at
 *      current fee).
 */
template <typename TIn, typename TOut>
std::optional<TAmounts<TIn, TOut>>
getAMMOfferStartWithTakerGets(
    TAmounts<TIn, TOut> const& pool,
    Quality const& targetQuality,
    std::uint16_t const& tfee)
{
    if (targetQuality.rate() == beast::kZERO)
        return std::nullopt;

    NumberRoundModeGuard const mg(Number::RoundingMode::ToNearest);
    auto const f = feeMult(tfee);
    auto const a = 1;
    auto const b = pool.in * (1 - 1 / f) / targetQuality.rate() - 2 * pool.out;
    auto const c = pool.out * pool.out - (pool.in * pool.out) / targetQuality.rate();

    auto nTakerGets = solveQuadraticEqSmallest(a, b, c);
    if (!nTakerGets || *nTakerGets <= 0)
        return std::nullopt;  // LCOV_EXCL_LINE

    auto const nTakerGetsConstraint = pool.out - pool.in / (targetQuality.rate() * f);
    if (nTakerGetsConstraint <= 0)
        return std::nullopt;

    // Select the smallest to maximize the quality
    if (nTakerGetsConstraint < *nTakerGets)
        nTakerGets = nTakerGetsConstraint;

    auto getAmounts = [&pool, &tfee](Number const& nTakerGetsProposed) {
        // Round downward to minimize the offer and to maximize the quality.
        // This has the most impact when takerGets is XRP.
        auto const takerGets =
            toAmount<TOut>(getAsset(pool.out), nTakerGetsProposed, Number::RoundingMode::Downward);
        return TAmounts<TIn, TOut>{swapAssetOut(pool, takerGets, tfee), takerGets};
    };

    // Try to reduce the offer size to improve the quality.
    // The quality might still not match the targetQuality for a tiny offer.
    auto amounts = getAmounts(*nTakerGets);
    if (Quality{amounts} < targetQuality)
        return getAmounts(detail::reduceOffer(amounts.out));
    return amounts;
}

/** Generate a synthetic AMM offer whose quality matches @p targetQuality,
 *  starting from takerPays (XRP in, or IOU/IOU).
 *
 *  Used for XRP-in/IOU-out and IOU/IOU pools.  Starting from the XRP
 *  side (takerPays) under `fixAMMv1_1` keeps rounding effects favorable.
 *
 *  Two binding constraints are solved and the smaller takerPays is chosen:
 *  - Scenario A — post-swap spot price equals @p targetQuality:
 *    `i²·f + i·I·(1+f) + I² - I·O/Qt = 0`
 *  - Scenario B — effective offer price equals @p targetQuality:
 *    `i = O/Qt - I/f`
 *
 *  where `O = poolPays`, `I = poolGets`, `f = feeMult(tfee)`.
 *  takerGets is then derived from the swap-in equation.  If the resulting
 *  offer quality is still below @p targetQuality after rounding, a 99.99%
 *  rescale via `detail::reduceOffer` is attempted.
 *
 *  @tparam TIn  Asset type flowing into the pool.
 *  @tparam TOut Asset type flowing out of the pool.
 *  @param pool Current AMM pool balances (`in` = poolGets, `out` = poolPays).
 *  @param targetQuality Desired offer quality (CLOB best quality).
 *  @param tfee Trading fee in basis points.
 *  @return Seated `{takerPays, takerGets}` amounts, or `std::nullopt` if a
 *      valid offer cannot be generated.
 */
template <typename TIn, typename TOut>
std::optional<TAmounts<TIn, TOut>>
getAMMOfferStartWithTakerPays(
    TAmounts<TIn, TOut> const& pool,
    Quality const& targetQuality,
    std::uint16_t tfee)
{
    if (targetQuality.rate() == beast::kZERO)
        return std::nullopt;

    NumberRoundModeGuard const mg(Number::RoundingMode::ToNearest);
    auto const f = feeMult(tfee);
    auto const& a = f;
    auto const b = pool.in * (1 + f);
    auto const c = pool.in * pool.in - pool.in * pool.out * targetQuality.rate();

    auto nTakerPays = solveQuadraticEqSmallest(a, b, c);
    if (!nTakerPays || nTakerPays <= 0)
        return std::nullopt;  // LCOV_EXCL_LINE

    auto const nTakerPaysConstraint = pool.out * targetQuality.rate() - pool.in / f;
    if (nTakerPaysConstraint <= 0)
        return std::nullopt;

    // Select the smallest to maximize the quality
    if (nTakerPaysConstraint < *nTakerPays)
        nTakerPays = nTakerPaysConstraint;

    auto getAmounts = [&pool, &tfee](Number const& nTakerPaysProposed) {
        // Round downward to minimize the offer and to maximize the quality.
        // This has the most impact when takerPays is XRP.
        auto const takerPays =
            toAmount<TIn>(getAsset(pool.in), nTakerPaysProposed, Number::RoundingMode::Downward);
        return TAmounts<TIn, TOut>{takerPays, swapAssetIn(pool, takerPays, tfee)};
    };

    // Try to reduce the offer size to improve the quality.
    // The quality might still not match the targetQuality for a tiny offer.
    auto amounts = getAmounts(*nTakerPays);
    if (Quality{amounts} < targetQuality)
        return getAmounts(detail::reduceOffer(amounts.in));
    return amounts;
}

/** Generate a synthetic AMM offer that aligns the pool's spot price with a CLOB quality.
 *
 *  The payment engine calls this when it encounters both AMM pools and order
 *  book offers for the same currency pair.  The resulting offer has a quality
 *  such that either the post-swap spot price equals @p quality (AMM offer
 *  quality is better) or the offer's effective price equals @p quality (the
 *  post-swap spot price is better) — whichever produces the smaller offer.
 *
 *  Amendment behavior:
 *  - Pre-`fixAMMv1_1`: always solves for takerPays first; rounding down XRP
 *    takerGets can push quality below target, causing the offer to be rejected.
 *  - Post-`fixAMMv1_1`: solves for the XRP side first (takerGets when pool pays
 *    XRP, takerPays otherwise) so XRP rounding improves rather than degrades
 *    quality.  Falls back to `detail::reduceOffer` if quality is still below
 *    target after rounding.
 *
 *  A quality mismatch larger than 1e-7 is logged at `j.error()` level; smaller
 *  mismatches are trace-only.
 *
 *  @tparam TIn  Asset type flowing into the pool.
 *  @tparam TOut Asset type flowing out of the pool.
 *  @param pool Current AMM pool balances.
 *  @param quality Target quality (best CLOB offer quality for this pair).
 *  @param tfee Trading fee in basis points.
 *  @param rules Current ledger rules (for amendment checks).
 *  @param j Journal for diagnostic logging.
 *  @return Seated `{takerPays, takerGets}` amounts, or `std::nullopt` if the
 *      quality cannot be achieved (generally at high fees).
 */
template <typename TIn, typename TOut>
std::optional<TAmounts<TIn, TOut>>
changeSpotPriceQuality(
    TAmounts<TIn, TOut> const& pool,
    Quality const& quality,
    std::uint16_t tfee,
    Rules const& rules,
    beast::Journal j)
{
    if (!rules.enabled(fixAMMv1_1))
    {
        // Finds takerPays (i) and takerGets (o) such that given pool
        // composition poolGets(I) and poolPays(O): (O - o) / (I + i) = quality.
        // Where takerGets is calculated as the swapAssetIn (see below).
        // The above equation produces the quadratic equation:
        // i^2*(1-fee) + i*I*(2-fee) + I^2 - I*O/quality,
        // which is solved for i, and o is found with swapAssetIn().
        auto const f = feeMult(tfee);  // 1 - fee
        auto const& a = f;
        auto const b = pool.in * (1 + f);
        Number const c = pool.in * pool.in - pool.in * pool.out * quality.rate();
        auto const res = b * b - 4 * a * c;
        if (res < 0)
        {
            return std::nullopt;  // LCOV_EXCL_LINE
        }
        if (auto const nTakerPaysPropose = (-b + root2(res)) / (2 * a); nTakerPaysPropose > 0)
        {
            auto const nTakerPays = [&]() {
                // The fee might make the AMM offer quality less than CLOB
                // quality. Therefore, AMM offer has to satisfy this constraint:
                // o / i >= q. Substituting o with swapAssetIn() gives: i <= O /
                // q - I / (1 - fee).
                auto const nTakerPaysConstraint = pool.out * quality.rate() - pool.in / f;
                if (nTakerPaysPropose > nTakerPaysConstraint)
                    return nTakerPaysConstraint;
                return nTakerPaysPropose;
            }();
            if (nTakerPays <= 0)
            {
                JLOG(j.trace()) << "changeSpotPriceQuality calc failed: " << to_string(pool.in)
                                << " " << to_string(pool.out) << " " << quality << " " << tfee;
                return std::nullopt;
            }
            auto const takerPays =
                toAmount<TIn>(getAsset(pool.in), nTakerPays, Number::RoundingMode::Upward);
            // should not fail
            if (auto amounts = TAmounts<TIn, TOut>{takerPays, swapAssetIn(pool, takerPays, tfee)};
                Quality{amounts} < quality &&
                !withinRelativeDistance(Quality{amounts}, quality, Number(1, -7)))
            {
                JLOG(j.error()) << "changeSpotPriceQuality failed: " << to_string(pool.in) << " "
                                << to_string(pool.out) << " "
                                << " " << quality << " " << tfee << " " << to_string(amounts.in)
                                << " " << to_string(amounts.out);
                Throw<std::runtime_error>("changeSpotPriceQuality failed");
            }
            else
            {
                JLOG(j.trace()) << "changeSpotPriceQuality succeeded: " << to_string(pool.in) << " "
                                << to_string(pool.out) << " "
                                << " " << quality << " " << tfee << " " << to_string(amounts.in)
                                << " " << to_string(amounts.out);
                return amounts;
            }
        }
        JLOG(j.trace()) << "changeSpotPriceQuality calc failed: " << to_string(pool.in) << " "
                        << to_string(pool.out) << " " << quality << " " << tfee;
        return std::nullopt;
    }

    // Generate the offer starting with XRP side. Return seated offer amounts
    // if the offer can be generated, otherwise nullopt.
    auto amounts = [&]() {
        if (isXRP(getAsset(pool.out)))
            return getAMMOfferStartWithTakerGets(pool, quality, tfee);
        return getAMMOfferStartWithTakerPays(pool, quality, tfee);
    }();
    if (!amounts)
    {
        JLOG(j.trace()) << "changeSpotPrice calc failed: " << to_string(pool.in) << " "
                        << to_string(pool.out) << " " << quality << " " << tfee;
        return std::nullopt;
    }

    if (Quality{*amounts} < quality)
    {
        JLOG(j.error()) << "changeSpotPriceQuality failed: " << to_string(pool.in) << " "
                        << to_string(pool.out) << " " << quality << " " << tfee << " "
                        << to_string(amounts->in) << " " << to_string(amounts->out);
        return std::nullopt;
    }

    JLOG(j.trace()) << "changeSpotPriceQuality succeeded: " << to_string(pool.in) << " "
                    << to_string(pool.out) << " "
                    << " " << quality << " " << tfee << " " << to_string(amounts->in) << " "
                    << to_string(amounts->out);

    return amounts;
}

// --- Swap-in / Swap-out ---

/** Deposit @p assetIn into the pool and receive a proportional amount of the
 *  other asset (AMM Swap in, XLS-30d).
 *
 *  Formula: `out = pool.out - (pool.in × pool.out) / (pool.in + assetIn × feeMult(tfee))`
 *
 *  Pool invariant: `(pool.in + assetIn) × (pool.out - out) >= pool.in × pool.out`.
 *  XRP integer rounding can violate this; post-`fixAMMv1_1` each sub-expression
 *  has an explicitly directed rounding mode so the pool retains a tiny surplus.
 *  The output is always rounded downward so the trader receives less, not more.
 *
 *  @tparam TIn  Asset type deposited (poolGets side).
 *  @tparam TOut Asset type received (poolPays side).
 *  @param pool Current AMM pool balances.
 *  @param assetIn Amount being deposited into the pool.
 *  @param tfee Trading fee in basis points.
 *  @return Amount of the output asset the trader receives; zero if the pool
 *      denominator is non-positive.
 *  @see [XLS-30d AMM Swap](https://github.com/XRPLF/XRPL-Standards/discussions/78)
 */
template <typename TIn, typename TOut>
TOut
swapAssetIn(TAmounts<TIn, TOut> const& pool, TIn const& assetIn, std::uint16_t tfee)
{
    if (auto const& rules = getCurrentTransactionRules(); rules && rules->enabled(fixAMMv1_1))
    {
        // set rounding to always favor the amm. Clip to zero.
        // calculate:
        // pool.out -
        // (pool.in * pool.out) / (pool.in + assetIn * feeMult(tfee)),
        // and explicitly set the rounding modes
        // Favoring the amm means we should:
        // minimize:
        // pool.out -
        // (pool.in * pool.out) / (pool.in + assetIn * feeMult(tfee)),
        // maximize:
        // (pool.in * pool.out) / (pool.in + assetIn * feeMult(tfee)),
        // (pool.in * pool.out)
        // minimize:
        // (pool.in + assetIn * feeMult(tfee)),
        // minimize:
        // assetIn * feeMult(tfee)
        // feeMult is: (1-fee), fee is tfee/100000
        // minimize:
        // 1-fee
        // maximize:
        // fee
        SaveNumberRoundMode const _{Number::getround()};

        Number::setround(Number::RoundingMode::Upward);
        auto const numerator = pool.in * pool.out;
        auto const fee = getFee(tfee);

        Number::setround(Number::RoundingMode::Downward);
        auto const denom = pool.in + assetIn * (1 - fee);

        if (denom.signum() <= 0)
            return toAmount<TOut>(getAsset(pool.out), 0);

        Number::setround(Number::RoundingMode::Upward);
        auto const ratio = numerator / denom;

        Number::setround(Number::RoundingMode::Downward);
        auto const swapOut = pool.out - ratio;

        if (swapOut.signum() < 0)
            return toAmount<TOut>(getAsset(pool.out), 0);

        return toAmount<TOut>(getAsset(pool.out), swapOut, Number::RoundingMode::Downward);
    }

    return toAmount<TOut>(
        getAsset(pool.out),
        pool.out - (pool.in * pool.out) / (pool.in + assetIn * feeMult(tfee)),
        Number::RoundingMode::Downward);
}

/** Withdraw @p assetOut from the pool and compute the required input asset (AMM Swap out, XLS-30d).
 *
 *  Formula: `in = ((pool.in × pool.out) / (pool.out - assetOut) - pool.in) / feeMult(tfee)`
 *
 *  The input is always rounded upward so the trader pays at least what the
 *  pool needs to maintain its invariant.  Post-`fixAMMv1_1` each intermediate
 *  step is individually directed; if the pool denominator is non-positive (i.e.
 *  @p assetOut >= the entire pool), the maximum representable `TIn` is returned.
 *
 *  @tparam TIn  Asset type deposited (poolGets side).
 *  @tparam TOut Asset type withdrawn (poolPays side).
 *  @param pool Current AMM pool balances.
 *  @param assetOut Amount being withdrawn from the pool.
 *  @param tfee Trading fee in basis points.
 *  @return Amount of the input asset the trader must pay; `toMaxAmount<TIn>`
 *      if the requested output would exhaust the pool.
 *  @see [XLS-30d AMM Swap](https://github.com/XRPLF/XRPL-Standards/discussions/78)
 */
template <typename TIn, typename TOut>
TIn
swapAssetOut(TAmounts<TIn, TOut> const& pool, TOut const& assetOut, std::uint16_t tfee)
{
    if (auto const& rules = getCurrentTransactionRules(); rules && rules->enabled(fixAMMv1_1))
    {
        // set rounding to always favor the amm. Clip to zero.
        // calculate:
        // ((pool.in * pool.out) / (pool.out - assetOut) - pool.in) /
        // (1-tfee/100000)
        // maximize:
        // ((pool.in * pool.out) / (pool.out - assetOut) - pool.in)
        // maximize:
        // (pool.in * pool.out) / (pool.out - assetOut)
        // maximize:
        // (pool.in * pool.out)
        // minimize
        // (pool.out - assetOut)
        // minimize:
        // (1-tfee/100000)
        // maximize:
        // tfee/100000

        SaveNumberRoundMode const _{Number::getround()};

        Number::setround(Number::RoundingMode::Upward);
        auto const numerator = pool.in * pool.out;

        Number::setround(Number::RoundingMode::Downward);
        auto const denom = pool.out - assetOut;
        if (denom.signum() <= 0)
        {
            return toMaxAmount<TIn>(getAsset(pool.in));
        }

        Number::setround(Number::RoundingMode::Upward);
        auto const ratio = numerator / denom;
        auto const numerator2 = ratio - pool.in;
        auto const fee = getFee(tfee);

        Number::setround(Number::RoundingMode::Downward);
        auto const feeMult = 1 - fee;

        Number::setround(Number::RoundingMode::Upward);
        auto const swapIn = numerator2 / feeMult;
        if (swapIn.signum() < 0)
            return toAmount<TIn>(getAsset(pool.in), 0);

        return toAmount<TIn>(getAsset(pool.in), swapIn, Number::RoundingMode::Upward);
    }

    return toAmount<TIn>(
        getAsset(pool.in),
        ((pool.in * pool.out) / (pool.out - assetOut) - pool.in) / feeMult(tfee),
        Number::RoundingMode::Upward);
}

/** Return `n²`. */
Number
square(Number const& n);

/** Adjust LP tokens to account for 16-digit precision loss in the running balance.
 *
 *  Adding newly-minted tokens to an already-large `lptAMMBalance` can lose
 *  significance in the least-significant digit: the stored balance advances
 *  by less than `lpTokens`.  This function round-trips through the 16-digit
 *  representation by computing `(balance + tokens) - balance` (deposit) or
 *  `(tokens - balance) + balance` (withdraw), returning the value that will
 *  actually be committed to the ledger.  Result is forced downward to ensure
 *  the adjusted tokens do not exceed the requested tokens.
 *
 *  @param lptAMMBalance Current total LP token supply stored on the AMM SLE.
 *  @param lpTokens Tokens being minted or burned.
 *  @param isDeposit `IsDeposit::Yes` for deposit, `IsDeposit::No` for withdrawal.
 *  @return Adjusted token amount that exactly matches the representable delta
 *      in the 16-digit balance.
 */
STAmount
adjustLPTokens(STAmount const& lptAMMBalance, STAmount const& lpTokens, IsDeposit isDeposit);

/** Adjust deposit/withdrawal asset amounts to match the precision-corrected LP token count.
 *
 *  Calls `adjustLPTokens()` to compute the representable token delta.  If the
 *  adjusted count is less than @p lpTokens, the corresponding asset amounts are
 *  scaled down so the ledger does not grant assets that exceed what the LP token
 *  math supports.  A no-op when `fixAMMv1_3` is active because `getRoundedLPTokens`
 *  already incorporates the precision adjustment.
 *
 *  @param amountBalance Current pool balance of the primary asset.
 *  @param amount Primary asset amount to deposit or withdraw.
 *  @param amount2 Secondary asset amount for two-sided operations; `std::nullopt`
 *      for single-asset operations.
 *  @param lptAMMBalance Current total LP token supply.
 *  @param lpTokens Calculated LP tokens before precision adjustment.
 *  @param tfee Trading fee in basis points.
 *  @param isDeposit `IsDeposit::Yes` for deposit, `IsDeposit::No` for withdrawal.
 *  @return Tuple of `(adjustedAmount, adjustedAmount2, adjustedLPTokens)`.
 */
std::tuple<STAmount, std::optional<STAmount>, STAmount>
adjustAmountsByLPTokens(
    STAmount const& amountBalance,
    STAmount const& amount,
    std::optional<STAmount> const& amount2,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens,
    std::uint16_t tfee,
    IsDeposit isDeposit);

/** Positive root of `a·x² + b·x + c = 0` using the standard formula.
 *
 *  Computes `x = (-b + sqrt(b² - 4·a·c)) / (2·a)`.  Used by `ammAssetIn`
 *  to invert Equation 4; the discriminant is guaranteed non-negative by the
 *  deposit formula's domain.
 *
 *  @param a Quadratic coefficient.
 *  @param b Linear coefficient.
 *  @param c Constant term.
 *  @return The positive root.
 */
Number
solveQuadraticEq(Number const& a, Number const& b, Number const& c);

/** Multiply @p amount by @p frac with an explicitly directed rounding mode.
 *
 *  Installs @p rm for both the `Number` multiplication and the subsequent
 *  `toSTAmount` conversion so that rounding is applied once at the final step,
 *  not accumulated through intermediates.  This is the building block for all
 *  `fixAMMv1_3` directional-rounding paths.
 *
 *  @param amount Base `STAmount` to scale.
 *  @param frac Scaling factor.
 *  @param rm Rounding mode to apply at the final conversion step.
 *  @return `amount × frac` rounded according to @p rm, expressed in the same
 *      asset as @p amount.
 */
STAmount
multiply(STAmount const& amount, Number const& frac, Number::RoundingMode rm);

namespace detail {

/** Select the LP token rounding direction that preserves the pool invariant.
 *
 *  Deposit: round downward (fewer tokens minted → pool worth more per token).
 *  Withdraw: round upward (more tokens burned → pool retains slightly more).
 *
 *  @param isDeposit Direction of the operation.
 *  @return `Downward` for deposit, `Upward` for withdrawal.
 */
inline Number::RoundingMode
getLPTokenRounding(IsDeposit isDeposit)
{
    // Minimize on deposit, maximize on withdraw to ensure
    // AMM invariant sqrt(poolAsset1 * poolAsset2) >= LPTokensBalance
    return isDeposit == IsDeposit::Yes ? Number::RoundingMode::Downward
                                       : Number::RoundingMode::Upward;
}

/** Select the asset rounding direction that preserves the pool invariant.
 *
 *  Deposit: round upward (depositor pays slightly more → pool is larger).
 *  Withdraw: round downward (withdrawer receives slightly less → pool retains).
 *
 *  @param isDeposit Direction of the operation.
 *  @return `Upward` for deposit, `Downward` for withdrawal.
 */
inline Number::RoundingMode
getAssetRounding(IsDeposit isDeposit)
{
    // Maximize on deposit, minimize on withdraw to ensure
    // AMM invariant sqrt(poolAsset1 * poolAsset2) >= LPTokensBalance
    return isDeposit == IsDeposit::Yes ? Number::RoundingMode::Upward
                                       : Number::RoundingMode::Downward;
}

}  // namespace detail

/** Compute a proportional asset amount with amendment-gated directional rounding.
 *
 *  Used for two-sided (equal) deposit/withdrawal where the asset amount is
 *  `balance × frac`.  Under `fixAMMv1_3` the final multiplication is rounded
 *  via `detail::getAssetRounding` (upward on deposit, downward on withdraw).
 *  Without the amendment the result uses the current ambient rounding mode.
 *
 *  @tparam A Type of @p frac; either `STAmount` or `Number`.
 *  @param rules Current ledger rules.
 *  @param balance Pool balance of the asset.
 *  @param frac Fraction of the pool balance to apply.
 *  @param isDeposit Direction; controls rounding when `fixAMMv1_3` is active.
 *  @return `balance × frac` rounded to preserve the pool invariant.
 */
template <typename A>
STAmount
getRoundedAsset(Rules const& rules, STAmount const& balance, A const& frac, IsDeposit isDeposit)
{
    if (!rules.enabled(fixAMMv1_3))
    {
        if constexpr (std::is_same_v<A, STAmount>)
        {
            return multiply(balance, frac, balance.asset());
        }
        else
        {
            return toSTAmount(balance.asset(), balance * frac);
        }
    }
    auto const rm = detail::getAssetRounding(isDeposit);
    return multiply(balance, frac, rm);
}

/** Compute a single-asset deposit/withdrawal amount with amendment-gated rounding.
 *
 *  The callback form defers evaluation to avoid computing the formula twice:
 *  - Without `fixAMMv1_3`: calls `noRoundCb()` and converts without directed rounding.
 *  - With `fixAMMv1_3`, deposit: calls `multiply(balance, productCb(), rm)`.
 *  - With `fixAMMv1_3`, withdrawal: installs @p rm globally and calls `productCb()`
 *    so every arithmetic step inside the callback shares the same rounding direction.
 *
 *  @param rules Current ledger rules.
 *  @param noRoundCb Produces the unrounded result (pre-amendment path).
 *  @param balance Pool balance of the asset.
 *  @param productCb Produces the rounding fraction (post-amendment path).
 *  @param isDeposit Direction; controls which rounding mode is selected.
 *  @return Rounded asset amount preserving the pool invariant.
 */
STAmount
getRoundedAsset(
    Rules const& rules,
    std::function<Number()> const& noRoundCb,
    STAmount const& balance,
    std::function<Number()> const& productCb,
    IsDeposit isDeposit);

/** Compute a proportional LP token amount with amendment-gated rounding and precision adjustment.
 *
 *  Used for two-sided (equal) deposit/withdrawal.  Under `fixAMMv1_3` the
 *  multiplication `balance × frac` is rounded via `detail::getLPTokenRounding`,
 *  then `adjustLPTokens` corrects for the 16-digit precision loss introduced
 *  when adding the result to the running LP token balance.
 *
 *  @param rules Current ledger rules.
 *  @param balance Current total LP token supply.
 *  @param frac Fraction of the pool's LP supply to mint or burn.
 *  @param isDeposit Direction; controls rounding and sign of the adjustment.
 *  @return LP token amount after rounding and precision correction.
 */
STAmount
getRoundedLPTokens(
    Rules const& rules,
    STAmount const& balance,
    Number const& frac,
    IsDeposit isDeposit);

/** Compute a single-asset LP token amount with amendment-gated rounding and precision adjustment.
 *
 *  The callback form avoids evaluating the formula twice:
 *  - Without `fixAMMv1_3`: calls `noRoundCb()` with no directed rounding.
 *  - With `fixAMMv1_3`, deposit: installs the LP rounding mode globally and
 *    calls `productCb()` (all arithmetic inside shares the direction).
 *  - With `fixAMMv1_3`, withdrawal: calls `multiply(lptAMMBalance, productCb(), rm)`.
 *  In all post-amendment cases, `adjustLPTokens` then corrects for 16-digit
 *  precision loss in the running LP balance.
 *
 *  @param rules Current ledger rules.
 *  @param noRoundCb Produces the unrounded result (pre-amendment path).
 *  @param lptAMMBalance Current total LP token supply.
 *  @param productCb Produces the rounding fraction (post-amendment path).
 *  @param isDeposit Direction; controls rounding mode selection.
 *  @return LP token amount after rounding and precision correction.
 */
STAmount
getRoundedLPTokens(
    Rules const& rules,
    std::function<Number()> const& noRoundCb,
    STAmount const& lptAMMBalance,
    std::function<Number()> const& productCb,
    IsDeposit isDeposit);

/** Adjust a single-asset deposit amount to match the precision-corrected LP token count.
 *
 *  Under `fixAMMv1_3`: computes `ammAssetIn(balance, lptAMMBalance, tokens, tfee)`.
 *  If rounding causes the derived asset amount to exceed @p amount, the deposit is
 *  reduced by the overshoot and both tokens and asset are recomputed, then the minimum
 *  of original and adjusted amounts is returned.  Before the amendment, returns the
 *  inputs unchanged.
 *
 *  @param rules Current ledger rules.
 *  @param balance Pool balance of the asset being deposited.
 *  @param amount Requested deposit amount.
 *  @param lptAMMBalance Current total LP token supply.
 *  @param tokens LP token count before precision adjustment.
 *  @param tfee Trading fee in basis points.
 *  @return `{adjustedTokens, adjustedAmount}` pair.
 */
std::pair<STAmount, STAmount>
adjustAssetInByTokens(
    Rules const& rules,
    STAmount const& balance,
    STAmount const& amount,
    STAmount const& lptAMMBalance,
    STAmount const& tokens,
    std::uint16_t tfee);

/** Adjust a single-asset withdrawal amount to match the precision-corrected LP token count.
 *
 *  Under `fixAMMv1_3`: computes `ammAssetOut(balance, lptAMMBalance, tokens, tfee)`.
 *  If rounding causes the derived asset amount to exceed @p amount, the withdrawal is
 *  reduced by the overshoot and both tokens and asset are recomputed, then the minimum
 *  of original and adjusted amounts is returned.  Before the amendment, returns the
 *  inputs unchanged.
 *
 *  @param rules Current ledger rules.
 *  @param balance Pool balance of the asset being withdrawn.
 *  @param amount Requested withdrawal amount.
 *  @param lptAMMBalance Current total LP token supply.
 *  @param tokens LP token count before precision adjustment.
 *  @param tfee Trading fee in basis points.
 *  @return `{adjustedTokens, adjustedAmount}` pair.
 */
std::pair<STAmount, STAmount>
adjustAssetOutByTokens(
    Rules const& rules,
    STAmount const& balance,
    STAmount const& amount,
    STAmount const& lptAMMBalance,
    STAmount const& tokens,
    std::uint16_t tfee);

/** Recompute the LP token fraction after precision adjustment.
 *
 *  Under `fixAMMv1_3` the precision-adjusted token count may differ from the
 *  originally requested count, so the fraction `tokens / lptAMMBalance` must
 *  be recomputed from the adjusted value before it is used to scale equal
 *  deposit/withdrawal amounts.  Returns @p frac unchanged when `fixAMMv1_3`
 *  is inactive (the precision adjustment has not yet been applied).
 *
 *  @param rules Current ledger rules.
 *  @param lptAMMBalance Current total LP token supply.
 *  @param tokens Precision-adjusted LP token count.
 *  @param frac Original fraction before adjustment.
 *  @return Adjusted fraction `tokens / lptAMMBalance`, or @p frac if
 *      `fixAMMv1_3` is not active.
 */
Number
adjustFracByTokens(
    Rules const& rules,
    STAmount const& lptAMMBalance,
    STAmount const& tokens,
    Number const& frac);

/** Read the AMM's current pool asset balances from the ledger.
 *
 *  Delegates to `accountHolds` for each asset, respecting freeze and
 *  authorization policy.  Does not read the LP token balance.
 *
 *  @param view Ledger state to query.
 *  @param ammAccountID AccountID of the AMM's pseudo-account.
 *  @param asset1 First pool asset.
 *  @param asset2 Second pool asset.
 *  @param freezeHandling Whether to enforce freeze restrictions.
 *  @param authHandling Whether to enforce authorization restrictions.
 *  @param j Journal for diagnostic logging.
 *  @return `{balance1, balance2}` pair in the same asset order as the inputs.
 */
std::pair<STAmount, STAmount>
ammPoolHolds(
    ReadView const& view,
    AccountID const& ammAccountID,
    Asset const& asset1,
    Asset const& asset2,
    FreezeHandling freezeHandling,
    AuthHandling authHandling,
    beast::Journal const j);

/** Read the AMM's pool balances and total LP token supply from the ledger.
 *
 *  When both optional assets are provided they are validated against the AMM
 *  SLE's stored pair and used as the query order; providing only one resolves
 *  the counterpart from `ammSle`.  If neither is provided, the canonical order
 *  from `ammSle` is used.  An invalid asset pair (mismatched with the AMM SLE)
 *  indicates a corrupted AMM object and returns `tecAMM_INVALID_TOKENS`.
 *
 *  @param view Ledger state to query.
 *  @param ammSle The AMM's `ltAMM` SLE.
 *  @param optAsset1 Optional first asset override.
 *  @param optAsset2 Optional second asset override.
 *  @param freezeHandling Whether to enforce freeze restrictions.
 *  @param authHandling Whether to enforce authorization restrictions.
 *  @param j Journal for diagnostic logging.
 *  @return `{balance1, balance2, lpTokenBalance}` on success, or
 *      `Unexpected(tecAMM_INVALID_TOKENS)` if the asset pair is invalid.
 */
Expected<std::tuple<STAmount, STAmount, STAmount>, TER>
ammHolds(
    ReadView const& view,
    SLE const& ammSle,
    std::optional<Asset> const& optAsset1,
    std::optional<Asset> const& optAsset2,
    FreezeHandling freezeHandling,
    AuthHandling authHandling,
    beast::Journal const j);

/** Read an LP's token balance from its direct trustline with the AMM account.
 *
 *  Intentionally bypasses `accountHolds` — that function would also check
 *  whether the AMM's underlying pool assets are frozen (under
 *  `fixFrozenLPTokenTransfer`), which is incorrect policy for LP token balance
 *  queries.  Only the LP token trustline's own freeze flag is checked.
 *  Trust-line orientation: raw `sfBalance` is negated when `lpAccount > ammAccount`.
 *
 *  @param view Ledger state to query.
 *  @param asset1 First pool asset (used to derive the LP token currency).
 *  @param asset2 Second pool asset.
 *  @param ammAccount AccountID of the AMM's pseudo-account (LP token issuer).
 *  @param lpAccount AccountID of the liquidity provider.
 *  @param j Journal for diagnostic logging.
 *  @return The LP's token balance, or zero if the trustline is absent or frozen.
 */
STAmount
ammLPHolds(
    ReadView const& view,
    Asset const& asset1,
    Asset const& asset2,
    AccountID const& ammAccount,
    AccountID const& lpAccount,
    beast::Journal const j);

/** Read an LP's token balance using the asset pair stored in @p ammSle.
 *
 *  Convenience overload; extracts `sfAsset`, `sfAsset2`, and `sfAccount` from
 *  @p ammSle and delegates to the five-parameter `ammLPHolds`.
 *
 *  @param view Ledger state to query.
 *  @param ammSle The AMM's `ltAMM` SLE.
 *  @param lpAccount AccountID of the liquidity provider.
 *  @param j Journal for diagnostic logging.
 *  @return The LP's token balance, or zero if the trustline is absent or frozen.
 */
STAmount
ammLPHolds(
    ReadView const& view,
    SLE const& ammSle,
    AccountID const& lpAccount,
    beast::Journal const j);

/** Get the effective AMM trading fee for @p account.
 *
 *  Returns the auction slot's `sfDiscountedFee` if the slot is unexpired and
 *  @p account is either the slot owner or one of up to four authorized accounts;
 *  otherwise returns the AMM's global `sfTradingFee`.  Expiration is compared
 *  against the ledger's `parentCloseTime` (the slot stores
 *  `parentCloseTime + TOTAL_TIME_SLOT_SECS` at creation, i.e. 24 hours).
 *
 *  @param view Ledger state providing the current close time.
 *  @param ammSle The AMM's `ltAMM` SLE.
 *  @param account The account whose fee rate is needed.
 *  @return Fee rate in basis points (0–1000).
 */
std::uint16_t
getTradingFee(ReadView const& view, SLE const& ammSle, AccountID const& account);

/** Read the AMM account's raw pool-asset balance, bypassing balance hooks.
 *
 *  Unlike `accountHolds`, this function does not invoke `balanceHookIOU` or
 *  `balanceHookMPT`, so the result is unaffected by `PaymentSandbox`
 *  deferred-credit accounting.  Used when the AMM needs its own unmodified
 *  balance for math, not for payment routing.  Returns zero if the trustline
 *  or MPToken object is absent or frozen.
 *
 *  @param view Ledger state to query.
 *  @param ammAccountID AccountID of the AMM's pseudo-account.
 *  @param asset The pool asset to query (IOU, XRP, or MPT).
 *  @return The raw balance, or zero if unavailable.
 */
STAmount
ammAccountHolds(ReadView const& view, AccountID const& ammAccountID, Asset const& asset);

/** Remove all ledger objects owned by the AMM and, if successful, delete the AMM itself.
 *
 *  Deletion is ordered: IOU trustlines first, then MPToken objects, then the
 *  AMM SLE and its `AccountRoot`.  Because each ledger transaction has a bounded
 *  work budget, not all trustlines may be removable in one call; in that case
 *  `tecINCOMPLETE` is returned and the caller must submit additional transactions
 *  to finish.  The AMM can be re-deposited while deletion is incomplete.
 *
 *  @param view Sandbox for applying state changes.
 *  @param asset First pool asset (used to locate the AMM keylet).
 *  @param asset2 Second pool asset.
 *  @param j Journal for diagnostic logging.
 *  @return `tesSUCCESS` on full deletion, `tecINCOMPLETE` if trustlines remain,
 *      or `tecINTERNAL` for unexpected ledger inconsistencies.
 */
TER
deleteAMMAccount(Sandbox& view, Asset const& asset, Asset const& asset2, beast::Journal j);

/** Initialize the vote slot and auction slot on a new or re-created AMM.
 *
 *  Called on both `AMMCreate` and on `AMMDeposit` when the pool was previously
 *  drained to zero.  Sets up:
 *  - One vote entry for @p account with full weight (`kVOTE_WEIGHT_SCALE_FACTOR`).
 *  - An auction slot owned by @p account, expiring in 24 hours, at zero price.
 *  - `sfDiscountedFee` = `tfee / kAUCTION_SLOT_DISCOUNTED_FEE_FRACTION`.
 *  - Absent-field canonicalization: fee fields are removed if their value is zero.
 *  - Under `fixCleanup3_2_0`, stale `sfAuthAccounts` from any previous slot owner
 *    are cleared.
 *
 *  @param view Apply-view for the current transaction.
 *  @param ammSle The AMM's `ltAMM` SLE (modified in place).
 *  @param account The creator/re-depositor receiving the slot.
 *  @param lptAsset The LP token asset descriptor (used as the `sfPrice` currency).
 *  @param tfee Trading fee in basis points to set.
 */
void
initializeFeeAuctionVote(
    ApplyView& view,
    std::shared_ptr<SLE>& ammSle,
    AccountID const& account,
    Asset const& lptAsset,
    std::uint16_t tfee);

/** Determine whether @p lpAccount is the sole remaining liquidity provider.
 *
 *  Walks the AMM account's owner directory (up to 10 pages, covering at most
 *  4 objects) counting LPToken trustlines, pool-asset trustlines, MPToken
 *  objects, and the AMM SLE itself.  Any second LPToken trustline belonging to
 *  a different account returns `false` immediately.
 *
 *  @param view Ledger state to query.
 *  @param ammIssue The LP token issue (currency + AMM account as issuer).
 *  @param lpAccount AccountID of the candidate sole LP.
 *  @return `true` if @p lpAccount is the only LP, `false` if other LPs exist,
 *      or `Unexpected(tecINTERNAL)` for any unexpected directory state
 *      (e.g. more than one LPToken trustline for @p lpAccount).
 */
Expected<bool, TER>
isOnlyLiquidityProvider(ReadView const& view, Issue const& ammIssue, AccountID const& lpAccount);

/** Reconcile the AMM's `sfLPTokenBalance` with the last LP's trustline balance.
 *
 *  Accumulated rounding over the life of the pool can cause the AMM's running
 *  `sfLPTokenBalance` to differ slightly from the sole LP's trustline balance.
 *  This function:
 *  1. Confirms @p account is the only remaining LP via `isOnlyLiquidityProvider`.
 *  2. If so, verifies the discrepancy is within 0.1% (tolerance `1e-3`).
 *  3. If within tolerance, updates `sfLPTokenBalance` to @p lpTokens so the
 *     final withdrawal leaves the AMM in a fully consistent state.
 *
 *  @param sb Sandbox for applying the balance correction.
 *  @param lpTokens The last LP's actual trustline balance.
 *  @param ammSle The AMM's `ltAMM` SLE (updated in place if correction applied).
 *  @param account AccountID of the candidate sole LP.
 *  @return `true` if the balance was reconciled or no adjustment was needed
 *      (other LPs exist), `Unexpected(tecAMM_INVALID_TOKENS)` if the
 *      discrepancy exceeds tolerance, or `Unexpected(tecINTERNAL)` on an
 *      unexpected directory error.
 */
Expected<bool, TER>
verifyAndAdjustLPTokenBalance(
    Sandbox& sb,
    STAmount const& lpTokens,
    std::shared_ptr<SLE>& ammSle,
    AccountID const& account);

}  // namespace xrpl
