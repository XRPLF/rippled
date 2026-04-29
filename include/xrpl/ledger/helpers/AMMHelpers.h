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

Number
reduceOffer(auto const& amount)
{
    static Number const reducedOfferPct(9999, -4);

    // Make sure the result is always less than amount or zero.
    NumberRoundModeGuard const mg(Number::rounding_mode::towards_zero);
    return amount * reducedOfferPct;
}

}  // namespace detail

enum class IsDeposit : bool { No = false, Yes = true };

/** Calculate LP Tokens given AMM pool reserves.
 * @param asset1 AMM one side of the pool reserve
 * @param asset2 AMM another side of the pool reserve
 * @return LP Tokens as IOU
 */
STAmount
ammLPTokens(STAmount const& asset1, STAmount const& asset2, Asset const& lptIssue);

/** Calculate LP Tokens given asset's deposit amount.
 * @param asset1Balance current AMM asset1 balance
 * @param asset1Deposit requested asset1 deposit amount
 * @param lptAMMBalance AMM LPT balance
 * @param tfee trading fee in basis points
 * @return tokens
 */
STAmount
lpTokensOut(
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
lpTokensIn(
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
ammAssetOut(
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

/** Check if the relative distance between the amounts
 * is within the requested distance.
 * @param calc calculated amount
 * @param req requested amount
 * @param dist requested relative distance
 * @return true if within dist, false otherwise
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

    NumberRoundModeGuard const mg(Number::rounding_mode::to_nearest);
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
            toAmount<TOut>(getAsset(pool.out), nTakerGetsProposed, Number::rounding_mode::downward);
        return TAmounts<TIn, TOut>{swapAssetOut(pool, takerGets, tfee), takerGets};
    };

    // Try to reduce the offer size to improve the quality.
    // The quality might still not match the targetQuality for a tiny offer.
    auto amounts = getAmounts(*nTakerGets);
    if (Quality{amounts} < targetQuality)
        return getAmounts(detail::reduceOffer(amounts.out));
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

    NumberRoundModeGuard const mg(Number::rounding_mode::to_nearest);
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
            toAmount<TIn>(getAsset(pool.in), nTakerPaysProposed, Number::rounding_mode::downward);
        return TAmounts<TIn, TOut>{takerPays, swapAssetIn(pool, takerPays, tfee)};
    };

    // Try to reduce the offer size to improve the quality.
    // The quality might still not match the targetQuality for a tiny offer.
    auto amounts = getAmounts(*nTakerPays);
    if (Quality{amounts} < targetQuality)
        return getAmounts(detail::reduceOffer(amounts.in));
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
                toAmount<TIn>(getAsset(pool.in), nTakerPays, Number::rounding_mode::upward);
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
        saveNumberRoundMode const _{Number::getround()};

        Number::setround(Number::rounding_mode::upward);
        auto const numerator = pool.in * pool.out;
        auto const fee = getFee(tfee);

        Number::setround(Number::rounding_mode::downward);
        auto const denom = pool.in + assetIn * (1 - fee);

        if (denom.signum() <= 0)
            return toAmount<TOut>(getAsset(pool.out), 0);

        Number::setround(Number::rounding_mode::upward);
        auto const ratio = numerator / denom;

        Number::setround(Number::rounding_mode::downward);
        auto const swapOut = pool.out - ratio;

        if (swapOut.signum() < 0)
            return toAmount<TOut>(getAsset(pool.out), 0);

        return toAmount<TOut>(getAsset(pool.out), swapOut, Number::rounding_mode::downward);
    }

    return toAmount<TOut>(
        getAsset(pool.out),
        pool.out - (pool.in * pool.out) / (pool.in + assetIn * feeMult(tfee)),
        Number::rounding_mode::downward);
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

        saveNumberRoundMode const _{Number::getround()};

        Number::setround(Number::rounding_mode::upward);
        auto const numerator = pool.in * pool.out;

        Number::setround(Number::rounding_mode::downward);
        auto const denom = pool.out - assetOut;
        if (denom.signum() <= 0)
        {
            return toMaxAmount<TIn>(getAsset(pool.in));
        }

        Number::setround(Number::rounding_mode::upward);
        auto const ratio = numerator / denom;
        auto const numerator2 = ratio - pool.in;
        auto const fee = getFee(tfee);

        Number::setround(Number::rounding_mode::downward);
        auto const feeMult = 1 - fee;

        Number::setround(Number::rounding_mode::upward);
        auto const swapIn = numerator2 / feeMult;
        if (swapIn.signum() < 0)
            return toAmount<TIn>(getAsset(pool.in), 0);

        return toAmount<TIn>(getAsset(pool.in), swapIn, Number::rounding_mode::upward);
    }

    return toAmount<TIn>(
        getAsset(pool.in),
        ((pool.in * pool.out) / (pool.out - assetOut) - pool.in) / feeMult(tfee),
        Number::rounding_mode::upward);
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
 * @param isDeposit Yes if deposit, No if withdraw
 */
STAmount
adjustLPTokens(STAmount const& lptAMMBalance, STAmount const& lpTokens, IsDeposit isDeposit);

/** Calls adjustLPTokens() and adjusts deposit or withdraw amounts if
 * the adjusted LP tokens are less than the provided LP tokens.
 * @param amountBalance asset1 pool balance
 * @param amount asset1 to deposit or withdraw
 * @param amount2 asset2 to deposit or withdraw
 * @param lptAMMBalance LPT AMM Balance
 * @param lpTokens LP tokens to deposit or withdraw
 * @param tfee trading fee in basis points
 * @param isDeposit Yes if deposit, No if withdraw
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
    IsDeposit isDeposit);

/** Positive solution for quadratic equation:
 * x = (-b + sqrt(b**2 + 4*a*c))/(2*a)
 */
Number
solveQuadraticEq(Number const& a, Number const& b, Number const& c);

STAmount
multiply(STAmount const& amount, Number const& frac, Number::rounding_mode rm);

namespace detail {

inline Number::rounding_mode
getLPTokenRounding(IsDeposit isDeposit)
{
    // Minimize on deposit, maximize on withdraw to ensure
    // AMM invariant sqrt(poolAsset1 * poolAsset2) >= LPTokensBalance
    return isDeposit == IsDeposit::Yes ? Number::rounding_mode::downward
                                       : Number::rounding_mode::upward;
}

inline Number::rounding_mode
getAssetRounding(IsDeposit isDeposit)
{
    // Maximize on deposit, minimize on withdraw to ensure
    // AMM invariant sqrt(poolAsset1 * poolAsset2) >= LPTokensBalance
    return isDeposit == IsDeposit::Yes ? Number::rounding_mode::upward
                                       : Number::rounding_mode::downward;
}

}  // namespace detail

/** Round AMM equal deposit/withdrawal amount. Deposit/withdrawal formulas
 * calculate the amount as a fractional value of the pool balance. The rounding
 * takes place on the last step of multiplying the balance by the fraction if
 * AMMv1_3 is enabled.
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

/** Round AMM single deposit/withdrawal amount.
 * The lambda's are used to delay evaluation until the function
 * is executed so that the calculation is not done twice. noRoundCb() is
 * called if AMMv1_3 is disabled. Otherwise, the rounding is set and
 * the amount is:
 *   isDeposit is Yes - the balance multiplied by productCb()
 *   isDeposit is No - the result of productCb(). The rounding is
 *     the same for all calculations in productCb()
 */
STAmount
getRoundedAsset(
    Rules const& rules,
    std::function<Number()> const& noRoundCb,
    STAmount const& balance,
    std::function<Number()> const& productCb,
    IsDeposit isDeposit);

/** Round AMM deposit/withdrawal LPToken amount. Deposit/withdrawal formulas
 * calculate the lptokens as a fractional value of the AMM total lptokens.
 * The rounding takes place on the last step of multiplying the balance by
 * the fraction if AMMv1_3 is enabled. The tokens are then
 * adjusted to factor in the loss in precision (we only keep 16 significant
 * digits) when adding the lptokens to the balance.
 */
STAmount
getRoundedLPTokens(
    Rules const& rules,
    STAmount const& balance,
    Number const& frac,
    IsDeposit isDeposit);

/** Round AMM single deposit/withdrawal LPToken amount.
 * The lambda's are used to delay evaluation until the function is executed
 * so that the calculations are not done twice.
 * noRoundCb() is called if AMMv1_3 is disabled. Otherwise, the rounding is set
 * and the lptokens are:
 *   if isDeposit is Yes - the result of productCb(). The rounding is
 *     the same for all calculations in productCb()
 *   if isDeposit is No - the balance multiplied by productCb()
 * The lptokens are then adjusted to factor in the loss in precision
 * (we only keep 16 significant digits) when adding the lptokens to the balance.
 */
STAmount
getRoundedLPTokens(
    Rules const& rules,
    std::function<Number()> const& noRoundCb,
    STAmount const& lptAMMBalance,
    std::function<Number()> const& productCb,
    IsDeposit isDeposit);

/* Next two functions adjust asset in/out amount to factor in the adjusted
 * lptokens. The lptokens are calculated from the asset in/out. The lptokens are
 * then adjusted to factor in the loss in precision. The adjusted lptokens might
 * be less than the initially calculated tokens. Therefore, the asset in/out
 * must be adjusted. The rounding might result in the adjusted amount being
 * greater than the original asset in/out amount. If this happens,
 * then the original amount is reduced by the difference in the adjusted amount
 * and the original amount. The actual tokens and the actual adjusted amount
 * are then recalculated. The minimum of the original and the actual
 * adjusted amount is returned.
 */
std::pair<STAmount, STAmount>
adjustAssetInByTokens(
    Rules const& rules,
    STAmount const& balance,
    STAmount const& amount,
    STAmount const& lptAMMBalance,
    STAmount const& tokens,
    std::uint16_t tfee);
std::pair<STAmount, STAmount>
adjustAssetOutByTokens(
    Rules const& rules,
    STAmount const& balance,
    STAmount const& amount,
    STAmount const& lptAMMBalance,
    STAmount const& tokens,
    std::uint16_t tfee);

/** Find a fraction of tokens after the tokens are adjusted. The fraction
 * is used to adjust equal deposit/withdraw amount.
 */
Number
adjustFracByTokens(
    Rules const& rules,
    STAmount const& lptAMMBalance,
    STAmount const& tokens,
    Number const& frac);

/** Get AMM pool balances.
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

/** Get AMM pool and LP token balances. If both optIssue are
 * provided then they are used as the AMM token pair issues.
 * Otherwise the missing issues are fetched from ammSle.
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

/** Get the balance of LP tokens.
 */
STAmount
ammLPHolds(
    ReadView const& view,
    Asset const& asset1,
    Asset const& asset2,
    AccountID const& ammAccount,
    AccountID const& lpAccount,
    beast::Journal const j);

STAmount
ammLPHolds(
    ReadView const& view,
    SLE const& ammSle,
    AccountID const& lpAccount,
    beast::Journal const j);

/** Get AMM trading fee for the given account. The fee is discounted
 * if the account is the auction slot owner or one of the slot's authorized
 * accounts.
 */
std::uint16_t
getTradingFee(ReadView const& view, SLE const& ammSle, AccountID const& account);

/** Returns total amount held by AMM for the given token.
 */
STAmount
ammAccountHolds(ReadView const& view, AccountID const& ammAccountID, Asset const& asset);

/** Delete trustlines to AMM. If all trustlines are deleted then
 * AMM object and account are deleted. Otherwise tecINCOMPLETE is returned.
 */
TER
deleteAMMAccount(Sandbox& view, Asset const& asset, Asset const& asset2, beast::Journal j);

/** Initialize Auction and Voting slots and set the trading/discounted fee.
 */
void
initializeFeeAuctionVote(
    ApplyView& view,
    std::shared_ptr<SLE>& ammSle,
    AccountID const& account,
    Asset const& lptAsset,
    std::uint16_t tfee);

/** Return true if the Liquidity Provider is the only AMM provider, false
 * otherwise. Return tecINTERNAL if encountered an unexpected condition,
 * for instance Liquidity Provider has more than one LPToken trustline.
 */
Expected<bool, TER>
isOnlyLiquidityProvider(ReadView const& view, Issue const& ammIssue, AccountID const& lpAccount);

/** Due to rounding, the LPTokenBalance of the last LP might
 * not match the LP's trustline balance. If it's within the tolerance,
 * update LPTokenBalance to match the LP's trustline balance.
 */
Expected<bool, TER>
verifyAndAdjustLPTokenBalance(
    Sandbox& sb,
    STAmount const& lpTokens,
    std::shared_ptr<SLE>& ammSle,
    AccountID const& account);

}  // namespace xrpl
