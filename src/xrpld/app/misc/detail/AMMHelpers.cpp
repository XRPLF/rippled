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

#include <xrpld/app/misc/AMMHelpers.h>

namespace ripple {

STAmount
ammLPTokens(
    STAmount const& asset1,
    STAmount const& asset2,
    Issue const& lptIssue)
{
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
lpTokensIn(
    STAmount const& asset1Balance,
    STAmount const& asset1Deposit,
    STAmount const& lptAMMBalance,
    std::uint16_t tfee)
{
    auto const f1 = feeMult(tfee);
    auto const f2 = feeMultHalf(tfee) / f1;
    Number const r = asset1Deposit / asset1Balance;
    auto const c = root2(f2 * f2 + r / f1) - f2;
    auto const t = lptAMMBalance * (r - c) / (1 + c);
    return toSTAmount(lptAMMBalance.issue(), t);
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
    return toSTAmount(
        asset1Balance.issue(), asset1Balance * solveQuadraticEq(a, b, c));
}

/* Equation 7:
 * t = T * (c - sqrt(c**2 - 4*R))/2
 * where R = b/B, c = R*fee + 2 - fee
 */
STAmount
lpTokensOut(
    STAmount const& asset1Balance,
    STAmount const& asset1Withdraw,
    STAmount const& lptAMMBalance,
    std::uint16_t tfee)
{
    Number const fr = asset1Withdraw / asset1Balance;
    auto const f1 = getFee(tfee);
    auto const c = fr * f1 + 2 - f1;
    auto const t = lptAMMBalance * (c - root2(c * c - 4 * fr)) / 2;
    return toSTAmount(lptAMMBalance.issue(), t);
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
withdrawByTokens(
    STAmount const& assetBalance,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens,
    std::uint16_t tfee)
{
    auto const f = getFee(tfee);
    Number const t1 = lpTokens / lptAMMBalance;
    auto const b = assetBalance * (t1 * t1 - t1 * (2 - f)) / (t1 * f - 1);
    return toSTAmount(assetBalance.issue(), b);
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
    bool isDeposit)
{
    // Force rounding downward to ensure adjusted tokens are less or equal
    // to requested tokens.
    saveNumberRoundMode rm(Number::setround(Number::rounding_mode::downward));
    if (isDeposit)
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
    bool isDeposit)
{
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
            if (isDeposit)
                return ammAssetIn(
                    amountBalance, lptAMMBalance, lpTokensActual, tfee);
            else if (!ammRoundingEnabled)
                return withdrawByTokens(
                    amountBalance, lptAMMBalance, lpTokens, tfee);
            else
                return withdrawByTokens(
                    amountBalance, lptAMMBalance, lpTokensActual, tfee);
        }();
        if (!ammRoundingEnabled)
            return amountActual < amount
                ? std::make_tuple(amountActual, std::nullopt, lpTokensActual)
                : std::make_tuple(amount, std::nullopt, lpTokensActual);
        else
            return std::make_tuple(amountActual, std::nullopt, lpTokensActual);
    }

    ASSERT(
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

}  // namespace ripple
