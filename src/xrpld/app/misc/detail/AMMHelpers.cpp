#include <xrpld/app/misc/AMMHelpers.h>

namespace ripple {

STAmount
ammLPTokens(
    STAmount const& asset1,
    STAmount const& asset2,
    Issue const& lptIssue)
{
    // AMM invariant: sqrt(asset1 * asset2) >= LPTokensBalance
    auto const rounding =
        isFeatureEnabled(fixAMMv1_3) ? Number::downward : Number::getround();
    NumberRoundModeGuard g(rounding);
    auto const tokens = root2(asset1 * asset2);
    return toSTAmount(lptIssue, tokens);
}

/*
 * Equation 3:
 * t = T * [(b/B - (sqrt(f2**2 - b/(B*f1)) - f2)) /
 *          (1 + sqrt(f2**2 - b/(B*f1)) - f2)]
 * where f1 = 1 - tfee, f2 = (1 - tfee/2)/f1
 */
STAmount
lpTokensOut(
    STAmount const& asset1Balance,
    STAmount const& asset1Deposit,
    STAmount const& lptAMMBalance,
    std::uint16_t tfee)
{
    auto const f1 = feeMult(tfee);
    auto const f2 = feeMultHalf(tfee) / f1;
    Number const r = asset1Deposit / asset1Balance;
    auto const c = root2(f2 * f2 + r / f1) - f2;
    if (!isFeatureEnabled(fixAMMv1_3))
    {
        auto const t = lptAMMBalance * (r - c) / (1 + c);
        return toSTAmount(lptAMMBalance.issue(), t);
    }
    else
    {
        // minimize tokens out
        auto const frac = (r - c) / (1 + c);
        return multiply(lptAMMBalance, frac, Number::downward);
    }
}

/* Equation 4 solves equation 3 for b:
 * Let f1 = 1 - tfee, f2 = (1 - tfee/2)/f1, t1 = t/T, t2 = 1 + t1, R = b/B
 * then
 * t1 = [R - sqrt(f2**2 + R/f1) + f2] / [1 + sqrt(f2**2 + R/f1] - f2] =>
 * sqrt(f2**2 + R/f1)*(t1 + 1) = R + f2 + t1*f2 - t1 =>
 * sqrt(f2**2 + R/f1)*t2 = R + t2*f2 - t1 =>
 * sqrt(f2**2 + R/f1) = R/t2 + f2 - t1/t2, let d = f2 - t1/t2 =>
 * sqrt(f2**2 + R/f1) = R/t2 + d =>
 * f2**2 + R/f1 = (R/t2)**2 +2*d*R/t2 + d**2 =>
 * (R/t2)**2 + R*(2*d/t2 - 1/f1) + d**2 - f2**2 = 0
 */
STAmount
ammAssetIn(
    STAmount const& asset1Balance,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens,
    std::uint16_t tfee)
{
    auto const f1 = feeMult(tfee);
    auto const f2 = feeMultHalf(tfee) / f1;
    auto const t1 = lpTokens / lptAMMBalance;
    auto const t2 = 1 + t1;
    auto const d = f2 - t1 / t2;
    auto const a = 1 / (t2 * t2);
    auto const b = 2 * d / t2 - 1 / f1;
    auto const c = d * d - f2 * f2;
    if (!isFeatureEnabled(fixAMMv1_3))
    {
        return toSTAmount(
            asset1Balance.issue(), asset1Balance * solveQuadraticEq(a, b, c));
    }
    else
    {
        // maximize deposit
        auto const frac = solveQuadraticEq(a, b, c);
        return multiply(asset1Balance, frac, Number::upward);
    }
}

/* Equation 7:
 * t = T * (c - sqrt(c**2 - 4*R))/2
 * where R = b/B, c = R*fee + 2 - fee
 */
STAmount
lpTokensIn(
    STAmount const& asset1Balance,
    STAmount const& asset1Withdraw,
    STAmount const& lptAMMBalance,
    std::uint16_t tfee)
{
    Number const fr = asset1Withdraw / asset1Balance;
    auto const f1 = getFee(tfee);
    auto const c = fr * f1 + 2 - f1;
    if (!isFeatureEnabled(fixAMMv1_3))
    {
        auto const t = lptAMMBalance * (c - root2(c * c - 4 * fr)) / 2;
        return toSTAmount(lptAMMBalance.issue(), t);
    }
    else
    {
        // maximize tokens in
        auto const frac = (c - root2(c * c - 4 * fr)) / 2;
        return multiply(lptAMMBalance, frac, Number::upward);
    }
}

/* Equation 8 solves equation 7 for b:
 * c - 2*t/T = sqrt(c**2 - 4*R) =>
 * c**2 - 4*c*t/T + 4*t**2/T**2 = c**2 - 4*R =>
 * -4*c*t/T + 4*t**2/T**2 = -4*R =>
 * -c*t/T + t**2/T**2 = -R -=>
 * substitute c = R*f + 2 - f =>
 * -(t/T)*(R*f + 2 - f) + (t/T)**2 = -R, let t1 = t/T =>
 * -t1*R*f -2*t1 +t1*f +t1**2 = -R =>
 * R = (t1**2 + t1*(f - 2)) / (t1*f - 1)
 */
STAmount
ammAssetOut(
    STAmount const& assetBalance,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens,
    std::uint16_t tfee)
{
    auto const f = getFee(tfee);
    Number const t1 = lpTokens / lptAMMBalance;
    if (!isFeatureEnabled(fixAMMv1_3))
    {
        auto const b = assetBalance * (t1 * t1 - t1 * (2 - f)) / (t1 * f - 1);
        return toSTAmount(assetBalance.issue(), b);
    }
    else
    {
        // minimize withdraw
        auto const frac = (t1 * t1 - t1 * (2 - f)) / (t1 * f - 1);
        return multiply(assetBalance, frac, Number::downward);
    }
}

Number
square(Number const& n)
{
    return n * n;
}

STAmount
adjustLPTokens(
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens,
    IsDeposit isDeposit)
{
    // Force rounding downward to ensure adjusted tokens are less or equal
    // to requested tokens.
    saveNumberRoundMode rm(Number::setround(Number::rounding_mode::downward));
    if (isDeposit == IsDeposit::Yes)
        return (lptAMMBalance + lpTokens) - lptAMMBalance;
    return (lpTokens - lptAMMBalance) + lptAMMBalance;
}

std::tuple<STAmount, std::optional<STAmount>, STAmount>
adjustAmountsByLPTokens(
    STAmount const& amountBalance,
    STAmount const& amount,
    std::optional<STAmount> const& amount2,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens,
    std::uint16_t tfee,
    IsDeposit isDeposit)
{
    // AMMv1_3 amendment adjusts tokens and amounts in deposit/withdraw
    if (isFeatureEnabled(fixAMMv1_3))
        return std::make_tuple(amount, amount2, lpTokens);

    auto const lpTokensActual =
        adjustLPTokens(lptAMMBalance, lpTokens, isDeposit);

    if (lpTokensActual == beast::zero)
    {
        auto const amount2Opt =
            amount2 ? std::make_optional(STAmount{}) : std::nullopt;
        return std::make_tuple(STAmount{}, amount2Opt, lpTokensActual);
    }

    if (lpTokensActual < lpTokens)
    {
        bool const ammRoundingEnabled = [&]() {
            if (auto const& rules = getCurrentTransactionRules();
                rules && rules->enabled(fixAMMv1_1))
                return true;
            return false;
        }();

        // Equal trade
        if (amount2)
        {
            Number const fr = lpTokensActual / lpTokens;
            auto const amountActual = toSTAmount(amount.issue(), fr * amount);
            auto const amount2Actual =
                toSTAmount(amount2->issue(), fr * *amount2);
            if (!ammRoundingEnabled)
                return std::make_tuple(
                    amountActual < amount ? amountActual : amount,
                    amount2Actual < amount2 ? amount2Actual : amount2,
                    lpTokensActual);
            else
                return std::make_tuple(
                    amountActual, amount2Actual, lpTokensActual);
        }

        // Single trade
        auto const amountActual = [&]() {
            if (isDeposit == IsDeposit::Yes)
                return ammAssetIn(
                    amountBalance, lptAMMBalance, lpTokensActual, tfee);
            else if (!ammRoundingEnabled)
                return ammAssetOut(
                    amountBalance, lptAMMBalance, lpTokens, tfee);
            else
                return ammAssetOut(
                    amountBalance, lptAMMBalance, lpTokensActual, tfee);
        }();
        if (!ammRoundingEnabled)
            return amountActual < amount
                ? std::make_tuple(amountActual, std::nullopt, lpTokensActual)
                : std::make_tuple(amount, std::nullopt, lpTokensActual);
        else
            return std::make_tuple(amountActual, std::nullopt, lpTokensActual);
    }

    XRPL_ASSERT(
        lpTokensActual == lpTokens,
        "ripple::adjustAmountsByLPTokens : LP tokens match actual");

    return {amount, amount2, lpTokensActual};
}

Number
solveQuadraticEq(Number const& a, Number const& b, Number const& c)
{
    return (-b + root2(b * b - 4 * a * c)) / (2 * a);
}

// Minimize takerGets or takerPays
std::optional<Number>
solveQuadraticEqSmallest(Number const& a, Number const& b, Number const& c)
{
    auto const d = b * b - 4 * a * c;
    if (d < 0)
        return std::nullopt;
    // use numerically stable citardauq formula for quadratic equation solution
    // https://people.csail.mit.edu/bkph/articles/Quadratics.pdf
    if (b > 0)
        return (2 * c) / (-b - root2(d));
    else
        return (2 * c) / (-b + root2(d));
}

STAmount
multiply(STAmount const& amount, Number const& frac, Number::rounding_mode rm)
{
    NumberRoundModeGuard g(rm);
    auto const t = amount * frac;
    return toSTAmount(amount.issue(), t, rm);
}

STAmount
getRoundedAsset(
    Rules const& rules,
    std::function<Number()>&& noRoundCb,
    STAmount const& balance,
    std::function<Number()>&& productCb,
    IsDeposit isDeposit)
{
    if (!rules.enabled(fixAMMv1_3))
        return toSTAmount(balance.issue(), noRoundCb());

    auto const rm = detail::getAssetRounding(isDeposit);
    if (isDeposit == IsDeposit::Yes)
        return multiply(balance, productCb(), rm);
    NumberRoundModeGuard g(rm);
    return toSTAmount(balance.issue(), productCb(), rm);
}

STAmount
getRoundedLPTokens(
    Rules const& rules,
    STAmount const& balance,
    Number const& frac,
    IsDeposit isDeposit)
{
    if (!rules.enabled(fixAMMv1_3))
        return toSTAmount(balance.issue(), balance * frac);

    auto const rm = detail::getLPTokenRounding(isDeposit);
    auto const tokens = multiply(balance, frac, rm);
    return adjustLPTokens(balance, tokens, isDeposit);
}

STAmount
getRoundedLPTokens(
    Rules const& rules,
    std::function<Number()>&& noRoundCb,
    STAmount const& lptAMMBalance,
    std::function<Number()>&& productCb,
    IsDeposit isDeposit)
{
    if (!rules.enabled(fixAMMv1_3))
        return toSTAmount(lptAMMBalance.issue(), noRoundCb());

    auto const tokens = [&] {
        auto const rm = detail::getLPTokenRounding(isDeposit);
        if (isDeposit == IsDeposit::Yes)
        {
            NumberRoundModeGuard g(rm);
            return toSTAmount(lptAMMBalance.issue(), productCb(), rm);
        }
        return multiply(lptAMMBalance, productCb(), rm);
    }();
    return adjustLPTokens(lptAMMBalance, tokens, isDeposit);
}

std::pair<STAmount, STAmount>
adjustAssetInByTokens(
    Rules const& rules,
    STAmount const& balance,
    STAmount const& amount,
    STAmount const& lptAMMBalance,
    STAmount const& tokens,
    std::uint16_t tfee)
{
    if (!rules.enabled(fixAMMv1_3))
        return {tokens, amount};
    auto assetAdj = ammAssetIn(balance, lptAMMBalance, tokens, tfee);
    auto tokensAdj = tokens;
    // Rounding didn't work the right way.
    // Try to adjust the original deposit amount by difference
    // in adjust and original amount. Then adjust tokens and deposit amount.
    if (assetAdj > amount)
    {
        auto const adjAmount = amount - (assetAdj - amount);
        auto const t = lpTokensOut(balance, adjAmount, lptAMMBalance, tfee);
        tokensAdj = adjustLPTokens(lptAMMBalance, t, IsDeposit::Yes);
        assetAdj = ammAssetIn(balance, lptAMMBalance, tokensAdj, tfee);
    }
    return {tokensAdj, std::min(amount, assetAdj)};
}

std::pair<STAmount, STAmount>
adjustAssetOutByTokens(
    Rules const& rules,
    STAmount const& balance,
    STAmount const& amount,
    STAmount const& lptAMMBalance,
    STAmount const& tokens,
    std::uint16_t tfee)
{
    if (!rules.enabled(fixAMMv1_3))
        return {tokens, amount};
    auto assetAdj = ammAssetOut(balance, lptAMMBalance, tokens, tfee);
    auto tokensAdj = tokens;
    // Rounding didn't work the right way.
    // Try to adjust the original deposit amount by difference
    // in adjust and original amount. Then adjust tokens and deposit amount.
    if (assetAdj > amount)
    {
        auto const adjAmount = amount - (assetAdj - amount);
        auto const t = lpTokensIn(balance, adjAmount, lptAMMBalance, tfee);
        tokensAdj = adjustLPTokens(lptAMMBalance, t, IsDeposit::No);
        assetAdj = ammAssetOut(balance, lptAMMBalance, tokensAdj, tfee);
    }
    return {tokensAdj, std::min(amount, assetAdj)};
}

Number
adjustFracByTokens(
    Rules const& rules,
    STAmount const& lptAMMBalance,
    STAmount const& tokens,
    Number const& frac)
{
    if (!rules.enabled(fixAMMv1_3))
        return frac;
    return tokens / lptAMMBalance;
}

}  // namespace ripple
