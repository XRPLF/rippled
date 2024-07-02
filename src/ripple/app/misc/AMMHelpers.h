//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_APP_MISC_AMMHELPERS_H_INCLUDED
#define RIPPLE_APP_MISC_AMMHELPERS_H_INCLUDED

#include <ripple/basics/IOUAmount.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/Number.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/protocol/AMMCore.h>
#include <ripple/protocol/AmountConversions.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Issue.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/Rules.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STAmount.h>

#include <type_traits>

namespace ripple {

namespace detail {

Number
reduceOffer(auto const& amount)
{
    static Number const reducedOfferPct(9999, -4);

    // Make sure the result is always less than amount or zero.
    NumberRoundModeGuard mg(Number::towards_zero);
    return amount * reducedOfferPct;
}

}  // namespace detail

/** Calculate LP Tokens given AMM pool reserves.
 * @param asset1 AMM one side of the pool reserve
 * @param asset2 AMM another side of the pool reserve
 * @return LP Tokens as IOU
 */
STAmount
ammLPTokens(
    STAmount const& asset1,
    STAmount const& asset2,
    Issue const& lptIssue);

/** Calculate LP Tokens given asset's deposit amount.
 * @param asset1Balance current AMM asset1 balance
 * @param asset1Deposit requested asset1 deposit amount
 * @param lptAMMBalance AMM LPT balance
 * @param tfee trading fee in basis points
 * @return tokens
 */
STAmount
lpTokensIn(
    STAmount const& asset1Balance,
    STAmount const& asset1Deposit,
    STAmount const& lptAMMBalance,
    std::uint16_t tfee);

/** Calculate asset deposit given LP Tokens.
 * @param asset1Balance current AMM asset1 balance
 * @param lpTokens LP Tokens
 * @param lptAMMBalance AMM LPT balance
 * @param tfee trading fee in basis points
 * @return
 */
STAmount
ammAssetIn(
    STAmount const& asset1Balance,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens,
    std::uint16_t tfee);

/** Calculate LP Tokens given asset's withdraw amount. Return 0
 * if can't calculate.
 * @param asset1Balance current AMM asset1 balance
 * @param asset1Withdraw requested asset1 withdraw amount
 * @param lptAMMBalance AMM LPT balance
 * @param tfee trading fee in basis points
 * @return tokens out amount
 */
STAmount
lpTokensOut(
    STAmount const& asset1Balance,
    STAmount const& asset1Withdraw,
    STAmount const& lptAMMBalance,
    std::uint16_t tfee);

/** Calculate asset withdrawal by tokens
 * @param assetBalance balance of the asset being withdrawn
 * @param lptAMMBalance total AMM Tokens balance
 * @param lpTokens LP Tokens balance
 * @param tfee trading fee in basis points
 * @return calculated asset amount
 */
STAmount
withdrawByTokens(
    STAmount const& assetBalance,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens,
    std::uint16_t tfee);

/** Check if the relative distance between the qualities
 * is within the requested distance.
 * @param calcQuality calculated quality
 * @param reqQuality requested quality
 * @param dist requested relative distance
 * @return true if within dist, false otherwise
 */
inline bool
withinRelativeDistance(
    Quality const& calcQuality,
    Quality const& reqQuality,
    Number const& dist)
{
    if (calcQuality == reqQuality)
        return true;
    auto const [min, max] = std::minmax(calcQuality, reqQuality);
    // Relative distance is (max - min)/max. Can't use basic operations
    // on Quality. Have to use Quality::rate() instead, which
    // is inverse of quality: (1/max.rate - 1/min.rate)/(1/max.rate)
    return ((min.rate() - max.rate()) / min.rate()) < dist;
}

/** Check if the relative distance between the amounts
 * is within the requested distance.
 * @param calc calculated amount
 * @param req requested amount
 * @param dist requested relative distance
 * @return true if within dist, false otherwise
 */
// clang-format off
template <typename Amt>
    requires(
        std::is_same_v<Amt, STAmount> || std::is_same_v<Amt, IOUAmount> ||
        std::is_same_v<Amt, XRPAmount> || std::is_same_v<Amt, Number>)
bool
withinRelativeDistance(Amt const& calc, Amt const& req, Number const& dist)
{
    if (calc == req)
        return true;
    auto const [min, max] = std::minmax(calc, req);
    return ((max - min) / max) < dist;
}
// clang-format on

/** Solve quadratic equation to find takerGets or takerPays. Round
 * to minimize the amount in order to maximize the quality.
 */
std::optional<Number>
solveQuadraticEqSmallest(Number const& a, Number const& b, Number const& c);

/** Generate AMM offer starting with takerGets when AMM pool
 * from the payment perspective is IOU(in)/XRP(out)
 * Equations:
 * Spot Price Quality after the offer is consumed:
 *     Qsp = (O - o) / (I + i)    -- equation (1)
 *  where O is poolPays, I is poolGets, o is takerGets, i is takerPays
 * Swap out:
 *     i = (I * o) / (O - o) * f  -- equation (2)
 *  where f is (1 - tfee/100000), tfee is in basis points
 * Effective price targetQuality:
 *     Qep = o / i                -- equation (3)
 * There are two scenarios to consider
 * A) Qsp = Qep. Substitute i in (1) with (2) and solve for o
 *    and Qsp = targetQuality(Qt):
 *     o**2 + o * (I * Qt * (1 - 1 / f) - 2 * O) + O**2 - Qt * I * O = 0
 * B) Qep = Qsp. Substitute i in (3) with (2) and solve for o
 *    and Qep = targetQuality(Qt):
 *     o = O - I * Qt / f
 * Since the scenario is not known a priori, both A and B are solved and
 * the lowest value of o is takerGets. takerPays is calculated with
 * swap out eq (2). If o is less or equal to 0 then the offer can't
 * be generated.
 */
template <typename TIn, typename TOut>
std::optional<TAmounts<TIn, TOut>>
getAMMOfferStartWithTakerGets(
    TAmounts<TIn, TOut> const& pool,
    Quality const& targetQuality,
    std::uint16_t const& tfee)
{
    if (targetQuality.rate() == beast::zero)
        return std::nullopt;

    NumberRoundModeGuard mg(Number::to_nearest);
    auto const f = feeMult(tfee);
    auto const a = 1;
    auto const b = pool.in * (1 - 1 / f) / targetQuality.rate() - 2 * pool.out;
    auto const c =
        pool.out * pool.out - (pool.in * pool.out) / targetQuality.rate();

    auto nTakerGets = solveQuadraticEqSmallest(a, b, c);
    if (!nTakerGets || *nTakerGets <= 0)
        return std::nullopt;  // LCOV_EXCL_LINE

    auto const nTakerGetsConstraint =
        pool.out - pool.in / (targetQuality.rate() * f);
    if (nTakerGetsConstraint <= 0)
        return std::nullopt;

    // Select the smallest to maximize the quality
    if (nTakerGetsConstraint < *nTakerGets)
        nTakerGets = nTakerGetsConstraint;

    auto getAmounts = [&pool, &tfee](Number const& nTakerGetsProposed) {
        // Round downward to minimize the offer and to maximize the quality.
        // This has the most impact when takerGets is XRP.
        auto const takerGets = toAmount<TOut>(
            getIssue(pool.out), nTakerGetsProposed, Number::downward);
        return TAmounts<TIn, TOut>{
            swapAssetOut(pool, takerGets, tfee), takerGets};
    };

    // Try to reduce the offer size to improve the quality.
    // The quality might still not match the targetQuality for a tiny offer.
    if (auto const amounts = getAmounts(*nTakerGets);
        Quality{amounts} < targetQuality)
        return getAmounts(detail::reduceOffer(amounts.out));
    else
        return amounts;
}

/** Generate AMM offer starting with takerPays when AMM pool
 * from the payment perspective is XRP(in)/IOU(out) or IOU(in)/IOU(out).
 * Equations:
 * Spot Price Quality after the offer is consumed:
 *     Qsp = (O - o) / (I + i)       -- equation (1)
 *  where O is poolPays, I is poolGets, o is takerGets, i is takerPays
 * Swap in:
 *     o = (O * i * f) / (I + i * f) -- equation (2)
 *  where f is (1 - tfee/100000), tfee is in basis points
 * Effective price quality:
 *     Qep = o / i                   -- equation (3)
 * There are two scenarios to consider
 * A) Qsp = Qep. Substitute o in (1) with (2) and solve for i
 *    and Qsp = targetQuality(Qt):
 *     i**2 * f + i * I * (1 + f) + I**2 - I * O / Qt = 0
 * B) Qep = Qsp. Substitute i in (3) with (2) and solve for i
 *    and Qep = targetQuality(Qt):
 *     i = O / Qt - I / f
 * Since the scenario is not known a priori, both A and B are solved and
 * the lowest value of i is takerPays. takerGets is calculated with
 * swap in eq (2). If i is less or equal to 0 then the offer can't
 * be generated.
 */
template <typename TIn, typename TOut>
std::optional<TAmounts<TIn, TOut>>
getAMMOfferStartWithTakerPays(
    TAmounts<TIn, TOut> const& pool,
    Quality const& targetQuality,
    std::uint16_t tfee)
{
    if (targetQuality.rate() == beast::zero)
        return std::nullopt;

    NumberRoundModeGuard mg(Number::to_nearest);
    auto const f = feeMult(tfee);
    auto const& a = f;
    auto const b = pool.in * (1 + f);
    auto const c =
        pool.in * pool.in - pool.in * pool.out * targetQuality.rate();

    auto nTakerPays = solveQuadraticEqSmallest(a, b, c);
    if (!nTakerPays || nTakerPays <= 0)
        return std::nullopt;  // LCOV_EXCL_LINE

    auto const nTakerPaysConstraint =
        pool.out * targetQuality.rate() - pool.in / f;
    if (nTakerPaysConstraint <= 0)
        return std::nullopt;

    // Select the smallest to maximize the quality
    if (nTakerPaysConstraint < *nTakerPays)
        nTakerPays = nTakerPaysConstraint;

    auto getAmounts = [&pool, &tfee](Number const& nTakerPaysProposed) {
        // Round downward to minimize the offer and to maximize the quality.
        // This has the most impact when takerPays is XRP.
        auto const takerPays = toAmount<TIn>(
            getIssue(pool.in), nTakerPaysProposed, Number::downward);
        return TAmounts<TIn, TOut>{
            takerPays, swapAssetIn(pool, takerPays, tfee)};
    };

    // Try to reduce the offer size to improve the quality.
    // The quality might still not match the targetQuality for a tiny offer.
    if (auto const amounts = getAmounts(*nTakerPays);
        Quality{amounts} < targetQuality)
        return getAmounts(detail::reduceOffer(amounts.in));
    else
        return amounts;
}

/**   Generate AMM offer so that either updated Spot Price Quality (SPQ)
 * is equal to LOB quality (in this case AMM offer quality is
 * better than LOB quality) or AMM offer is equal to LOB quality
 * (in this case SPQ is better than LOB quality).
 * Pre-amendment code calculates takerPays first. If takerGets is XRP,
 * it is rounded down, which results in worse offer quality than
 * LOB quality, and the offer might fail to generate.
 * Post-amendment code calculates the XRP offer side first. The result
 * is rounded down, which makes the offer quality better.
 *   It might not be possible to match either SPQ or AMM offer to LOB
 * quality. This generally happens at higher fees.
 * @param pool AMM pool balances
 * @param quality requested quality
 * @param tfee trading fee in basis points
 * @return seated in/out amounts if the quality can be changed
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
        Number const c =
            pool.in * pool.in - pool.in * pool.out * quality.rate();
        if (auto const res = b * b - 4 * a * c; res < 0)
            return std::nullopt;  // LCOV_EXCL_LINE
        else if (auto const nTakerPaysPropose = (-b + root2(res)) / (2 * a);
                 nTakerPaysPropose > 0)
        {
            auto const nTakerPays = [&]() {
                // The fee might make the AMM offer quality less than CLOB
                // quality. Therefore, AMM offer has to satisfy this constraint:
                // o / i >= q. Substituting o with swapAssetIn() gives: i <= O /
                // q - I / (1 - fee).
                auto const nTakerPaysConstraint =
                    pool.out * quality.rate() - pool.in / f;
                if (nTakerPaysPropose > nTakerPaysConstraint)
                    return nTakerPaysConstraint;
                return nTakerPaysPropose;
            }();
            if (nTakerPays <= 0)
            {
                JLOG(j.trace())
                    << "changeSpotPriceQuality calc failed: "
                    << to_string(pool.in) << " " << to_string(pool.out) << " "
                    << quality << " " << tfee;
                return std::nullopt;
            }
            auto const takerPays =
                toAmount<TIn>(getIssue(pool.in), nTakerPays, Number::upward);
            // should not fail
            if (auto const amounts =
                    TAmounts<TIn, TOut>{
                        takerPays, swapAssetIn(pool, takerPays, tfee)};
                Quality{amounts} < quality &&
                !withinRelativeDistance(
                    Quality{amounts}, quality, Number(1, -7)))
            {
                JLOG(j.error())
                    << "changeSpotPriceQuality failed: " << to_string(pool.in)
                    << " " << to_string(pool.out) << " "
                    << " " << quality << " " << tfee << " "
                    << to_string(amounts.in) << " " << to_string(amounts.out);
                Throw<std::runtime_error>("changeSpotPriceQuality failed");
            }
            else
            {
                JLOG(j.trace())
                    << "changeSpotPriceQuality succeeded: "
                    << to_string(pool.in) << " " << to_string(pool.out) << " "
                    << " " << quality << " " << tfee << " "
                    << to_string(amounts.in) << " " << to_string(amounts.out);
                return amounts;
            }
        }
        JLOG(j.trace()) << "changeSpotPriceQuality calc failed: "
                        << to_string(pool.in) << " " << to_string(pool.out)
                        << " " << quality << " " << tfee;
        return std::nullopt;
    }

    // Generate the offer starting with XRP side. Return seated offer amounts
    // if the offer can be generated, otherwise nullopt.
    auto const amounts = [&]() {
        if (isXRP(getIssue(pool.out)))
            return getAMMOfferStartWithTakerGets(pool, quality, tfee);
        return getAMMOfferStartWithTakerPays(pool, quality, tfee);
    }();
    if (!amounts)
    {
        JLOG(j.trace()) << "changeSpotPrice calc failed: " << to_string(pool.in)
                        << " " << to_string(pool.out) << " " << quality << " "
                        << tfee << std::endl;
        return std::nullopt;
    }

    if (Quality{*amounts} < quality)
    {
        JLOG(j.error()) << "changeSpotPriceQuality failed: "
                        << to_string(pool.in) << " " << to_string(pool.out)
                        << " " << quality << " " << tfee << " "
                        << to_string(amounts->in) << " "
                        << to_string(amounts->out);
        return std::nullopt;
    }

    JLOG(j.trace()) << "changeSpotPriceQuality succeeded: "
                    << to_string(pool.in) << " " << to_string(pool.out) << " "
                    << " " << quality << " " << tfee << " "
                    << to_string(amounts->in) << " " << to_string(amounts->out);

    return amounts;
}

/** AMM pool invariant - the product (A * B) after swap in/out has to remain
 * at least the same: (A + in) * (B - out) >= A * B
 * XRP round-off may result in a smaller product after swap in/out.
 * To address this:
 *   - if on swapIn the out is XRP then the amount is round-off
 *     downward, making the product slightly larger since out
 *     value is reduced.
 *   - if on swapOut the in is XRP then the amount is round-off
 *     upward, making the product slightly larger since in
 *     value is increased.
 */

/** Swap assetIn into the pool and swap out a proportional amount
 * of the other asset. Implements AMM Swap in.
 * @see [XLS30d:AMM
 * Swap](https://github.com/XRPLF/XRPL-Standards/discussions/78)
 * @param pool current AMM pool balances
 * @param assetIn amount to swap in
 * @param tfee trading fee in basis points
 * @return
 */
template <typename TIn, typename TOut>
TOut
swapAssetIn(
    TAmounts<TIn, TOut> const& pool,
    TIn const& assetIn,
    std::uint16_t tfee)
{
    if (auto const& rules = getCurrentTransactionRules();
        rules && rules->enabled(fixAMMv1_1))
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
        saveNumberRoundMode _{Number::getround()};

        Number::setround(Number::upward);
        auto const numerator = pool.in * pool.out;
        auto const fee = getFee(tfee);

        Number::setround(Number::downward);
        auto const denom = pool.in + assetIn * (1 - fee);

        if (denom.signum() <= 0)
            return toAmount<TOut>(getIssue(pool.out), 0);

        Number::setround(Number::upward);
        auto const ratio = numerator / denom;

        Number::setround(Number::downward);
        auto const swapOut = pool.out - ratio;

        if (swapOut.signum() < 0)
            return toAmount<TOut>(getIssue(pool.out), 0);

        return toAmount<TOut>(getIssue(pool.out), swapOut, Number::downward);
    }
    else
    {
        return toAmount<TOut>(
            getIssue(pool.out),
            pool.out -
                (pool.in * pool.out) / (pool.in + assetIn * feeMult(tfee)),
            Number::downward);
    }
}

/** Swap assetOut out of the pool and swap in a proportional amount
 * of the other asset. Implements AMM Swap out.
 * @see [XLS30d:AMM
 * Swap](https://github.com/XRPLF/XRPL-Standards/discussions/78)
 * @param pool current AMM pool balances
 * @param assetOut amount to swap out
 * @param tfee trading fee in basis points
 * @return
 */
template <typename TIn, typename TOut>
TIn
swapAssetOut(
    TAmounts<TIn, TOut> const& pool,
    TOut const& assetOut,
    std::uint16_t tfee)
{
    if (auto const& rules = getCurrentTransactionRules();
        rules && rules->enabled(fixAMMv1_1))
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

        saveNumberRoundMode _{Number::getround()};

        Number::setround(Number::upward);
        auto const numerator = pool.in * pool.out;

        Number::setround(Number::downward);
        auto const denom = pool.out - assetOut;
        if (denom.signum() <= 0)
        {
            return toMaxAmount<TIn>(getIssue(pool.in));
        }

        Number::setround(Number::upward);
        auto const ratio = numerator / denom;
        auto const numerator2 = ratio - pool.in;
        auto const fee = getFee(tfee);

        Number::setround(Number::downward);
        auto const feeMult = 1 - fee;

        Number::setround(Number::upward);
        auto const swapIn = numerator2 / feeMult;
        if (swapIn.signum() < 0)
            return toAmount<TIn>(getIssue(pool.in), 0);

        return toAmount<TIn>(getIssue(pool.in), swapIn, Number::upward);
    }
    else
    {
        return toAmount<TIn>(
            getIssue(pool.in),
            ((pool.in * pool.out) / (pool.out - assetOut) - pool.in) /
                feeMult(tfee),
            Number::upward);
    }
}

/** Return square of n.
 */
Number
square(Number const& n);

/** Adjust LP tokens to deposit/withdraw.
 * Amount type keeps 16 digits. Maintaining the LP balance by adding
 * deposited tokens or subtracting withdrawn LP tokens from LP balance
 * results in losing precision in LP balance. I.e. the resulting LP balance
 * is less than the actual sum of LP tokens. To adjust for this, subtract
 * old tokens balance from the new one for deposit or vice versa for
 * withdraw to cancel out the precision loss.
 * @param lptAMMBalance LPT AMM Balance
 * @param lpTokens LP tokens to deposit or withdraw
 * @param isDeposit true if deposit, false if withdraw
 */
STAmount
adjustLPTokens(
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens,
    bool isDeposit);

/** Calls adjustLPTokens() and adjusts deposit or withdraw amounts if
 * the adjusted LP tokens are less than the provided LP tokens.
 * @param amountBalance asset1 pool balance
 * @param amount asset1 to deposit or withdraw
 * @param amount2 asset2 to deposit or withdraw
 * @param lptAMMBalance LPT AMM Balance
 * @param lpTokens LP tokens to deposit or withdraw
 * @param tfee trading fee in basis points
 * @param isDeposit true if deposit, false if withdraw
 * @return
 */
std::tuple<STAmount, std::optional<STAmount>, STAmount>
adjustAmountsByLPTokens(
    STAmount const& amountBalance,
    STAmount const& amount,
    std::optional<STAmount> const& amount2,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens,
    std::uint16_t tfee,
    bool isDeposit);

/** Positive solution for quadratic equation:
 * x = (-b + sqrt(b**2 + 4*a*c))/(2*a)
 */
Number
solveQuadraticEq(Number const& a, Number const& b, Number const& c);

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_AMMHELPERS_H_INCLUDED
