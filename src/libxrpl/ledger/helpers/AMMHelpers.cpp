/** @file AMMHelpers.cpp
 *  Implementation of the AMM mathematical engine and ledger-state utilities.
 *
 *  Provides the closed-form deposit/withdrawal formulas, quadratic solvers,
 *  directional rounding wrappers, pool balance readers, and AMM account
 *  lifecycle management.  The companion header additionally defines the
 *  inline swap and spot-price-quality templates that depend on these
 *  primitives.
 *
 *  All arithmetic that touches pool balances obeys the invariant:
 *  @code
 *    sqrt(asset1 × asset2) >= LPTokenBalance
 *  @endcode
 *  Rounding is always applied in the direction that keeps the pool at least
 *  as large as required by the invariant.  The `fixAMMv1_3` amendment
 *  introduces explicit per-step directional rounding; pre-amendment paths are
 *  preserved for historic replay.
 */
#include <xrpl/ledger/helpers/AMMHelpers.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

namespace xrpl {

STAmount
ammLPTokens(STAmount const& asset1, STAmount const& asset2, Asset const& lptIssue)
{
    // AMM invariant: sqrt(asset1 * asset2) >= LPTokensBalance
    auto const rounding =
        isFeatureEnabled(fixAMMv1_3) ? Number::RoundingMode::Downward : Number::getround();
    NumberRoundModeGuard const g(rounding);
    auto const tokens = root2(asset1 * asset2);
    return toSTAmount(lptIssue, tokens);
}

/** LP tokens minted for a single-asset deposit (Equation 3).
 *
 *  Implements the closed-form formula:
 *  @code
 *    t = T * [(b/B - (sqrt(f2**2 - b/(B*f1)) - f2)) /
 *             (1 + sqrt(f2**2 - b/(B*f1)) - f2)]
 *  @endcode
 *  where `f1 = 1 - tfee`, `f2 = (1 - tfee/2) / f1`, `b` is the deposit
 *  amount, `B` is the current pool balance, and `T` is the current LP token
 *  supply.  Under `fixAMMv1_3` the final multiplication is rounded downward
 *  so that fewer tokens are issued, preserving the pool invariant.
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
        return toSTAmount(lptAMMBalance.asset(), t);
    }

    // minimize tokens out
    auto const frac = (r - c) / (1 + c);
    return multiply(lptAMMBalance, frac, Number::RoundingMode::Downward);
}

/** Required asset deposit for a desired LP token amount (Equation 4).
 *
 *  Solves Equation 3 for the deposit `b` given desired tokens `t`.
 *  Let `f1 = 1 - tfee`, `f2 = (1 - tfee/2) / f1`, `t1 = t/T`, `t2 = 1 + t1`,
 *  `R = b/B`, `d = f2 - t1/t2`.  Rearranging Equation 3 yields:
 *  @code
 *    (R/t2)**2 + R*(2*d/t2 - 1/f1) + d**2 - f2**2 = 0
 *  @endcode
 *  which is solved by `solveQuadraticEq()`.  Under `fixAMMv1_3` the result is
 *  rounded upward so the depositor pays more, preserving the pool invariant.
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
        return toSTAmount(asset1Balance.asset(), asset1Balance * solveQuadraticEq(a, b, c));
    }

    // maximize deposit
    auto const frac = solveQuadraticEq(a, b, c);
    return multiply(asset1Balance, frac, Number::RoundingMode::Upward);
}

/** LP tokens to burn for a single-asset withdrawal (Equation 7).
 *
 *  Implements the closed-form formula:
 *  @code
 *    t = T * (c - sqrt(c**2 - 4*R)) / 2
 *  @endcode
 *  where `R = b/B` (withdrawal fraction of pool balance), `c = R*fee + 2 - fee`,
 *  and `fee = tfee / 100000`.  Under `fixAMMv1_3` the final multiplication is
 *  rounded upward so more tokens must be burned, preserving the pool invariant.
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
        return toSTAmount(lptAMMBalance.asset(), t);
    }

    // maximize tokens in
    auto const frac = (c - root2(c * c - 4 * fr)) / 2;
    return multiply(lptAMMBalance, frac, Number::RoundingMode::Upward);
}

/** Asset returned for a given LP token burn (Equation 8).
 *
 *  Solves Equation 7 for the withdrawal `b` given tokens `t`.  Let
 *  `t1 = t/T` and `f = tfee / 100000`.  Substituting `c = R*f + 2 - f` and
 *  rearranging gives the direct rational formula:
 *  @code
 *    R = (t1**2 + t1*(f - 2)) / (t1*f - 1)
 *  @endcode
 *  where `R = b/B`.  Under `fixAMMv1_3` the final multiplication is rounded
 *  downward so the withdrawer receives less, preserving the pool invariant.
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
        return toSTAmount(assetBalance.asset(), b);
    }

    // minimize withdraw
    auto const frac = (t1 * t1 - t1 * (2 - f)) / (t1 * f - 1);
    return multiply(assetBalance, frac, Number::RoundingMode::Downward);
}

/** Return the square of @p n. */
Number
square(Number const& n)
{
    return n * n;
}

STAmount
adjustLPTokens(STAmount const& lptAMMBalance, STAmount const& lpTokens, IsDeposit isDeposit)
{
    // Force rounding downward to ensure adjusted tokens are less or equal
    // to requested tokens.
    SaveNumberRoundMode const rm(Number::setround(Number::RoundingMode::Downward));
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
    if (isFeatureEnabled(fixAMMv1_3))
        return std::make_tuple(amount, amount2, lpTokens);

    auto const lpTokensActual = adjustLPTokens(lptAMMBalance, lpTokens, isDeposit);

    if (lpTokensActual == beast::kZERO)
    {
        auto const amount2Opt = amount2 ? std::make_optional(STAmount{}) : std::nullopt;
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
            auto const amountActual = toSTAmount(amount.asset(), fr * amount);
            auto const amount2Actual = toSTAmount(amount2->asset(), fr * *amount2);
            if (!ammRoundingEnabled)
            {
                return std::make_tuple(
                    amountActual < amount ? amountActual : amount,
                    amount2Actual < amount2 ? amount2Actual : amount2,
                    lpTokensActual);
            }

            return std::make_tuple(amountActual, amount2Actual, lpTokensActual);
        }

        // Single trade
        auto const amountActual = [&]() {
            if (isDeposit == IsDeposit::Yes)
            {
                return ammAssetIn(amountBalance, lptAMMBalance, lpTokensActual, tfee);
            }
            if (!ammRoundingEnabled)
            {
                return ammAssetOut(amountBalance, lptAMMBalance, lpTokens, tfee);
            }

            return ammAssetOut(amountBalance, lptAMMBalance, lpTokensActual, tfee);
        }();
        if (!ammRoundingEnabled)
        {
            return amountActual < amount
                ? std::make_tuple(amountActual, std::nullopt, lpTokensActual)
                : std::make_tuple(amount, std::nullopt, lpTokensActual);
        }

        return std::make_tuple(amountActual, std::nullopt, lpTokensActual);
    }

    XRPL_ASSERT(
        lpTokensActual == lpTokens, "xrpl::adjustAmountsByLPTokens : LP tokens match actual");

    return {amount, amount2, lpTokensActual};
}

Number
solveQuadraticEq(Number const& a, Number const& b, Number const& c)
{
    return (-b + root2(b * b - 4 * a * c)) / (2 * a);
}

/** Smallest positive root of `a*x**2 + b*x + c = 0`, used to minimize
 *  takerGets or takerPays in offer generation.
 *
 *  Uses the numerically stable "citardauq" formula (Blinn 2006): when `b > 0`
 *  it computes `2c / (-b - sqrt(d))` instead of the standard
 *  `(-b + sqrt(d)) / 2a`, avoiding catastrophic cancellation when the two
 *  terms under subtraction are nearly equal.
 *
 *  @return The smallest positive root, or `std::nullopt` when the discriminant
 *      is negative (no real solution exists).
 */
std::optional<Number>
solveQuadraticEqSmallest(Number const& a, Number const& b, Number const& c)
{
    auto const d = b * b - 4 * a * c;
    if (d < 0)
        return std::nullopt;
    // use numerically stable citardauq formula for quadratic equation solution
    // https://people.csail.mit.edu/bkph/articles/Quadratics.pdf
    if (b > 0)
    {
        return (2 * c) / (-b - root2(d));
    }

    return (2 * c) / (-b + root2(d));
}

/** Multiply @p amount by @p frac under the specified rounding mode @p rm.
 *
 *  Installs @p rm for the duration of both the `Number` multiplication and the
 *  subsequent `toSTAmount()` conversion, so that rounding is applied
 *  consistently at the final step — not accumulated through intermediate
 *  operations.
 */
STAmount
multiply(STAmount const& amount, Number const& frac, Number::RoundingMode rm)
{
    NumberRoundModeGuard const g(rm);
    auto const t = amount * frac;
    return toSTAmount(amount.asset(), t, rm);
}

STAmount
getRoundedAsset(
    Rules const& rules,
    std::function<Number()> const& noRoundCb,
    STAmount const& balance,
    std::function<Number()> const& productCb,
    IsDeposit isDeposit)
{
    if (!rules.enabled(fixAMMv1_3))
        return toSTAmount(balance.asset(), noRoundCb());

    auto const rm = detail::getAssetRounding(isDeposit);
    if (isDeposit == IsDeposit::Yes)
        return multiply(balance, productCb(), rm);
    NumberRoundModeGuard const g(rm);
    return toSTAmount(balance.asset(), productCb(), rm);
}

STAmount
getRoundedLPTokens(
    Rules const& rules,
    STAmount const& balance,
    Number const& frac,
    IsDeposit isDeposit)
{
    if (!rules.enabled(fixAMMv1_3))
        return toSTAmount(balance.asset(), balance * frac);

    auto const rm = detail::getLPTokenRounding(isDeposit);
    auto const tokens = multiply(balance, frac, rm);
    return adjustLPTokens(balance, tokens, isDeposit);
}

STAmount
getRoundedLPTokens(
    Rules const& rules,
    std::function<Number()> const& noRoundCb,
    STAmount const& lptAMMBalance,
    std::function<Number()> const& productCb,
    IsDeposit isDeposit)
{
    if (!rules.enabled(fixAMMv1_3))
        return toSTAmount(lptAMMBalance.asset(), noRoundCb());

    auto const tokens = [&] {
        auto const rm = detail::getLPTokenRounding(isDeposit);
        if (isDeposit == IsDeposit::Yes)
        {
            NumberRoundModeGuard const g(rm);
            return toSTAmount(lptAMMBalance.asset(), productCb(), rm);
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

/** Recalculate the pool fraction after LP token adjustment.
 *
 *  When `fixAMMv1_3` is active, the adjusted token count (post-precision-loss
 *  correction) may differ from the originally requested count, so the fraction
 *  `tokens / lptAMMBalance` is recomputed from the adjusted value.  Under
 *  earlier amendments the original @p frac is returned unchanged because
 *  `adjustLPTokens()` has not yet been applied.
 */
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

std::pair<STAmount, STAmount>
ammPoolHolds(
    ReadView const& view,
    AccountID const& ammAccountID,
    Asset const& asset1,
    Asset const& asset2,
    FreezeHandling freezeHandling,
    AuthHandling authHandling,
    beast::Journal const j)
{
    auto const assetInBalance =
        accountHolds(view, ammAccountID, asset1, freezeHandling, authHandling, j);
    auto const assetOutBalance =
        accountHolds(view, ammAccountID, asset2, freezeHandling, authHandling, j);
    return std::make_pair(assetInBalance, assetOutBalance);
}

Expected<std::tuple<STAmount, STAmount, STAmount>, TER>
ammHolds(
    ReadView const& view,
    SLE const& ammSle,
    std::optional<Asset> const& optAsset1,
    std::optional<Asset> const& optAsset2,
    FreezeHandling freezeHandling,
    AuthHandling authHandling,
    beast::Journal const j)
{
    auto const assets = [&]() -> std::optional<std::pair<Asset, Asset>> {
        auto const asset1 = ammSle[sfAsset];
        auto const asset2 = ammSle[sfAsset2];
        if (optAsset1 && optAsset2)
        {
            if (invalidAMMAssetPair(
                    *optAsset1, *optAsset2, std::make_optional(std::make_pair(asset1, asset2))))
            {
                // This error can only be hit if the AMM is corrupted
                // LCOV_EXCL_START
                JLOG(j.debug()) << "ammHolds: Invalid optAsset1 or optAsset2 " << *optAsset1 << " "
                                << *optAsset2;
                return std::nullopt;
                // LCOV_EXCL_STOP
            }
            return std::make_optional(std::make_pair(*optAsset1, *optAsset2));
        }
        auto const singleAsset = [&asset1, &asset2, &j](
                                     Asset checkIssue,
                                     char const* label) -> std::optional<std::pair<Asset, Asset>> {
            if (checkIssue == asset1)
            {
                return std::make_optional(std::make_pair(asset1, asset2));
            }
            if (checkIssue == asset2)
            {
                return std::make_optional(std::make_pair(asset2, asset1));
            }
            // Unreachable unless AMM corrupted.
            // LCOV_EXCL_START
            JLOG(j.debug()) << "ammHolds: Invalid " << label << " " << checkIssue;
            return std::nullopt;
            // LCOV_EXCL_STOP
        };
        if (optAsset1)
        {
            return singleAsset(*optAsset1, "optAsset1");
        }
        if (optAsset2)
        {
            // Cannot have Amount2 without Amount.
            return singleAsset(*optAsset2, "optAsset2");  // LCOV_EXCL_LINE
        }
        return std::make_optional(std::make_pair(asset1, asset2));
    }();
    if (!assets)
        return Unexpected(tecAMM_INVALID_TOKENS);
    auto const [amount1, amount2] = ammPoolHolds(
        view,
        ammSle.getAccountID(sfAccount),
        assets->first,
        assets->second,
        freezeHandling,
        authHandling,
        j);
    return std::make_tuple(amount1, amount2, ammSle[sfLPTokenBalance]);
}

/** LP token balance for a given account, checking only the LP token trustline.
 *
 *  Intentionally does not delegate to `accountHolds()`.  Under the
 *  `fixFrozenLPTokenTransfer` amendment, `accountHolds()` also checks whether
 *  the AMM's underlying pool assets are frozen, which would incorrectly block
 *  LP token transfers.  This function only checks whether the LP token
 *  trustline itself is frozen, which is the correct policy for balance queries.
 *
 *  Trust-line orientation: if `lpAccount > ammAccount` the raw `sfBalance` is
 *  stored from the AMM's perspective and must be negated before returning.
 */
STAmount
ammLPHolds(
    ReadView const& view,
    Asset const& asset1,
    Asset const& asset2,
    AccountID const& ammAccount,
    AccountID const& lpAccount,
    beast::Journal const j)
{

    auto const currency = ammLPTCurrency(asset1, asset2);
    STAmount amount;

    auto const sle = view.read(keylet::line(lpAccount, ammAccount, currency));
    if (!sle)
    {
        amount.clear(Issue{currency, ammAccount});
        JLOG(j.trace()) << "ammLPHolds: no SLE "
                        << " lpAccount=" << to_string(lpAccount)
                        << " amount=" << amount.getFullText();
    }
    else if (isFrozen(view, lpAccount, currency, ammAccount))
    {
        amount.clear(Issue{currency, ammAccount});
        JLOG(j.trace()) << "ammLPHolds: frozen currency "
                        << " lpAccount=" << to_string(lpAccount)
                        << " amount=" << amount.getFullText();
    }
    else
    {
        amount = sle->getFieldAmount(sfBalance);
        if (lpAccount > ammAccount)
        {
            // Put balance in account terms.
            amount.negate();
        }
        amount.get<Issue>().account = ammAccount;

        JLOG(j.trace()) << "ammLPHolds:"
                        << " lpAccount=" << to_string(lpAccount)
                        << " amount=" << amount.getFullText();
    }

    return view.balanceHookIOU(lpAccount, ammAccount, amount);
}

STAmount
ammLPHolds(
    ReadView const& view,
    SLE const& ammSle,
    AccountID const& lpAccount,
    beast::Journal const j)
{
    return ammLPHolds(view, ammSle[sfAsset], ammSle[sfAsset2], ammSle[sfAccount], lpAccount, j);
}

/** Effective trading fee for @p account on the given AMM.
 *
 *  Returns `sfDiscountedFee` if the AMM's auction slot is unexpired and
 *  @p account is either the slot owner or one of its authorized accounts;
 *  otherwise returns the global `sfTradingFee`.  Expiration is compared
 *  against `parentCloseTime` in seconds (the slot stores
 *  `parentCloseTime + TOTAL_TIME_SLOT_SECS` at creation time, i.e. 24 hours).
 *
 *  @note When `fixInnerObjTemplate` is active an `sfAuctionSlot` object is
 *      always present; the assert verifies this invariant in debug builds.
 */
std::uint16_t
getTradingFee(ReadView const& view, SLE const& ammSle, AccountID const& account)
{
    using namespace std::chrono;
    XRPL_ASSERT(
        !view.rules().enabled(fixInnerObjTemplate) || ammSle.isFieldPresent(sfAuctionSlot),
        "xrpl::getTradingFee : auction present");
    if (ammSle.isFieldPresent(sfAuctionSlot))
    {
        auto const& auctionSlot = safeDowncast<STObject const&>(ammSle.peekAtField(sfAuctionSlot));
        // Not expired
        if (auto const expiration = auctionSlot[~sfExpiration];
            duration_cast<seconds>(view.header().parentCloseTime.time_since_epoch()).count() <
            expiration)
        {
            if (auctionSlot[~sfAccount] == account)
                return auctionSlot[sfDiscountedFee];
            if (auctionSlot.isFieldPresent(sfAuthAccounts))
            {
                for (auto const& acct : auctionSlot.getFieldArray(sfAuthAccounts))
                {
                    if (acct[~sfAccount] == account)
                        return auctionSlot[sfDiscountedFee];
                }
            }
        }
    }
    return ammSle[sfTradingFee];
}

/** Raw pool-asset balance of the AMM account, bypassing balance hooks.
 *
 *  Unlike `accountHolds()`, this function does not invoke `balanceHookIOU` or
 *  `balanceHookMPT` so the result is unaffected by `PaymentSandbox` deferred-
 *  credit accounting.  Used when the AMM needs its own unmodified balance for
 *  mathematical calculations, not for payment routing.
 */
STAmount
ammAccountHolds(ReadView const& view, AccountID const& ammAccountID, Asset const& asset)
{
    return asset.visit(
        [&](MPTIssue const& issue) {
            if (auto const sle = view.read(keylet::mptoken(issue, ammAccountID));
                sle && !isFrozen(view, ammAccountID, issue))
                return STAmount{issue, (*sle)[sfMPTAmount]};
            return STAmount{asset};
        },
        [&](Issue const& issue) {
            if (isXRP(issue))
            {
                if (auto const sle = view.read(keylet::account(ammAccountID)))
                    return (*sle)[sfBalance];
            }
            else if (
                auto const sle =
                    view.read(keylet::line(ammAccountID, issue.account, issue.currency));
                sle && !isFrozen(view, ammAccountID, issue.currency, issue.account))
            {
                STAmount amount = (*sle)[sfBalance];
                if (ammAccountID > issue.account)
                    amount.negate();
                amount.get<Issue>().account = issue.account;
                return amount;
            }
            return STAmount{asset};
        });
}

/** Delete zero-balance IOU trustlines from the AMM account's owner directory.
 *
 *  Walks the owner directory via `cleanupOnAccountDelete`, skipping `ltAMM`
 *  and `ltMPTOKEN` entries.  Every `ltRIPPLE_STATE` (IOU trustline) is
 *  deleted via `deleteAMMTrustLine()`; a non-zero balance is a protocol
 *  invariant violation and returns `tecINTERNAL` (annotated `LCOV_EXCL`).
 *
 *  @param maxTrustlinesToDelete Maximum number of trustlines to remove in a
 *      single call.  Returns `tecINCOMPLETE` if the directory still has
 *      entries after the limit is reached, enabling multi-transaction deletion.
 */
static TER
deleteAMMTrustLines(
    Sandbox& sb,
    AccountID const& ammAccountID,
    std::uint16_t maxTrustlinesToDelete,
    beast::Journal j)
{
    return cleanupOnAccountDelete(
        sb,
        keylet::ownerDir(ammAccountID),
        [&](LedgerEntryType nodeType,
            uint256 const&,
            std::shared_ptr<SLE>& sleItem) -> std::pair<TER, SkipEntry> {
            // Skip AMM and MPToken
            if (nodeType == ltAMM || nodeType == ltMPTOKEN)
                return {tesSUCCESS, SkipEntry::Yes};

            if (nodeType == ltRIPPLE_STATE)
            {
                // Trustlines must have zero balance
                if (sleItem->getFieldAmount(sfBalance) != beast::kZERO)
                {
                    // LCOV_EXCL_START
                    JLOG(j.error()) << "deleteAMMObjects: deleting trustline with "
                                       "non-zero balance.";
                    return {tecINTERNAL, SkipEntry::No};
                    // LCOV_EXCL_STOP
                }

                return {deleteAMMTrustLine(sb, sleItem, ammAccountID, j), SkipEntry::No};
            }
            // LCOV_EXCL_START
            JLOG(j.error()) << "deleteAMMObjects: deleting non-trustline or non-MPT " << nodeType;
            return {tecINTERNAL, SkipEntry::No};
            // LCOV_EXCL_STOP
        },
        j,
        maxTrustlinesToDelete);
}

/** Delete MPToken objects held by the AMM account.
 *
 *  Must be called only after `deleteAMMTrustLines()` has succeeded; if any
 *  `ltRIPPLE_STATE` entry remains, the function returns `tecINTERNAL` because
 *  trustlines must be fully cleared before MPTokens are removed.
 *  The directory is expected to contain at most three entries: two MPToken
 *  objects (one per pool-side MPT) plus the AMM SLE itself, so a fixed limit
 *  of 3 is sufficient.
 */
static TER
deleteAMMMPTokens(Sandbox& sb, AccountID const& ammAccountID, beast::Journal j)
{
    return cleanupOnAccountDelete(
        sb,
        keylet::ownerDir(ammAccountID),
        [&](LedgerEntryType nodeType,
            uint256 const&,
            std::shared_ptr<SLE>& sleItem) -> std::pair<TER, SkipEntry> {
            // Skip AMM
            if (nodeType == ltAMM)
                return {tesSUCCESS, SkipEntry::Yes};

            if (nodeType == ltMPTOKEN)
            {
                // MPT must have zero balance
                if (sleItem->getFieldU64(sfMPTAmount) != 0 ||
                    (*sleItem)[~sfLockedAmount].valueOr(0) != 0)
                {
                    // LCOV_EXCL_START
                    JLOG(j.error()) << "deleteAMMObjects: deleting MPT with "
                                       "non-zero balance.";
                    return {tecINTERNAL, SkipEntry::No};
                    // LCOV_EXCL_STOP
                }

                return {deleteAMMMPToken(sb, sleItem, ammAccountID, j), SkipEntry::No};
            }
            if (nodeType == ltRIPPLE_STATE)
            {
                // Trustlines should have been deleted
                // LCOV_EXCL_START
                JLOG(j.error()) << "deleteAMMObjects: trustlines should have been deleted";
                return {tecINTERNAL, SkipEntry::No};
                // LCOV_EXCL_STOP
            }
            // LCOV_EXCL_START
            JLOG(j.error()) << "deleteAMMObjects: deleting non-trustline or non-MPT " << nodeType;
            return {tecINTERNAL, SkipEntry::No};
            // LCOV_EXCL_STOP
        },
        j,
        3);  // At most two MPToken plus AMM object
}

/** Fully delete the AMM account, its SLE, and all associated ledger objects.
 *
 *  Deletion order is significant:
 *  1. `deleteAMMTrustLines()` — removes IOU trustlines (up to
 *     `kMAX_DELETABLE_AMM_TRUST_LINES`); returns `tecINCOMPLETE` if more
 *     remain, allowing a follow-up transaction to continue.
 *  2. `deleteAMMMPTokens()` — removes MPToken objects; only runs after all
 *     trustlines are gone so that the AMM can be re-created via deposit if
 *     step 1 returned `tecINCOMPLETE`.
 *  3. Owner-directory link removal, `emptyDirDelete`, and erase of both the
 *     AMM SLE and AMM root `AccountRoot` SLE.
 *
 *  @return `tesSUCCESS` on full deletion, `tecINCOMPLETE` if trustlines
 *      remain, or `tecINTERNAL` for unexpected ledger inconsistencies
 *      (annotated `LCOV_EXCL` — unreachable under correct business logic).
 */
TER
deleteAMMAccount(Sandbox& sb, Asset const& asset, Asset const& asset2, beast::Journal j)
{
    auto ammSle = sb.peek(keylet::amm(asset, asset2));
    if (!ammSle)
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "deleteAMMAccount: AMM object does not exist " << asset << " " << asset2;
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    auto const ammAccountID = (*ammSle)[sfAccount];
    auto sleAMMRoot = sb.peek(keylet::account(ammAccountID));
    if (!sleAMMRoot)
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "deleteAMMAccount: AMM account does not exist "
                        << to_string(ammAccountID);
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    if (auto const ter = deleteAMMTrustLines(sb, ammAccountID, kMAX_DELETABLE_AMM_TRUST_LINES, j);
        !isTesSuccess(ter))
        return ter;

    if (auto const ter = deleteAMMMPTokens(sb, ammAccountID, j); !isTesSuccess(ter))
        return ter;

    auto const ownerDirKeylet = keylet::ownerDir(ammAccountID);
    if (!sb.dirRemove(ownerDirKeylet, (*ammSle)[sfOwnerNode], ammSle->key(), false))
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "deleteAMMAccount: failed to remove dir link";
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }
    if (sb.exists(ownerDirKeylet) && !sb.emptyDirDelete(ownerDirKeylet))
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "deleteAMMAccount: cannot delete root dir node of "
                        << toBase58(ammAccountID);
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    sb.erase(ammSle);
    sb.erase(sleAMMRoot);

    return tesSUCCESS;
}

/** Initialize the fee vote slot and auction slot on a new or re-created AMM.
 *
 *  Called both on `AMMCreate` and on `AMMDeposit` when the AMM was empty
 *  (all liquidity withdrawn).  Writes a single vote entry with
 *  `kVOTE_WEIGHT_SCALE_FACTOR` (100% weight) for @p account, then
 *  sets up the auction slot with:
 *  - expiration = `parentCloseTime + kTOTAL_TIME_SLOT_SECS` (24 hours)
 *  - `sfPrice` = 0 (the creator receives the slot for free)
 *  - `sfDiscountedFee` = `tfee / kAUCTION_SLOT_DISCOUNTED_FEE_FRACTION` (1/10
 *    of the full fee)
 *
 *  Both fee fields are removed via `makeFieldAbsent()` when the computed value
 *  is zero, preserving canonical absent-field serialization.  Under
 *  `fixCleanup3_2_0`, any stale `sfAuthAccounts` from a previous slot owner
 *  is also cleared.
 */
void
initializeFeeAuctionVote(
    ApplyView& view,
    std::shared_ptr<SLE>& ammSle,
    AccountID const& account,
    Asset const& lptAsset,
    std::uint16_t tfee)
{
    auto const& rules = view.rules();
    STArray voteSlots;
    STObject voteEntry = STObject::makeInnerObject(sfVoteEntry);
    if (tfee != 0)
        voteEntry.setFieldU16(sfTradingFee, tfee);
    voteEntry.setFieldU32(sfVoteWeight, kVOTE_WEIGHT_SCALE_FACTOR);
    voteEntry.setAccountID(sfAccount, account);
    voteSlots.pushBack(voteEntry);
    ammSle->setFieldArray(sfVoteSlots, voteSlots);
    // Under fixInnerObjTemplate the slot object must be explicitly created if
    // absent (first AMMCreate path); on AMMDeposit re-init it already exists.
    if (rules.enabled(fixInnerObjTemplate) && !ammSle->isFieldPresent(sfAuctionSlot))
    {
        STObject auctionSlot = STObject::makeInnerObject(sfAuctionSlot);
        ammSle->set(std::move(auctionSlot));
    }
    STObject& auctionSlot = ammSle->peekFieldObject(sfAuctionSlot);
    auctionSlot.setAccountID(sfAccount, account);
    auto const expiration = std::chrono::duration_cast<std::chrono::seconds>(
                                view.header().parentCloseTime.time_since_epoch())
                                .count() +
        kTOTAL_TIME_SLOT_SECS;
    auctionSlot.setFieldU32(sfExpiration, expiration);
    auctionSlot.setFieldAmount(sfPrice, STAmount{lptAsset, 0});
    if (tfee != 0)
    {
        ammSle->setFieldU16(sfTradingFee, tfee);
    }
    else if (ammSle->isFieldPresent(sfTradingFee))
    {
        ammSle->makeFieldAbsent(sfTradingFee);  // LCOV_EXCL_LINE
    }
    if (auto const dfee = tfee / kAUCTION_SLOT_DISCOUNTED_FEE_FRACTION)
    {
        auctionSlot.setFieldU16(sfDiscountedFee, dfee);
    }
    else if (auctionSlot.isFieldPresent(sfDiscountedFee))
    {
        auctionSlot.makeFieldAbsent(sfDiscountedFee);  // LCOV_EXCL_LINE
    }
    // Clear stale auth accounts from any previous auction slot holder.
    if (rules.enabled(fixCleanup3_2_0) && auctionSlot.isFieldPresent(sfAuthAccounts))
        auctionSlot.makeFieldAbsent(sfAuthAccounts);
}

/** Determine whether @p lpAccount is the sole remaining liquidity provider.
 *
 *  Walks up to 10 pages of the AMM account's owner directory (sufficient for
 *  at most four objects: 1 AMM SLE + 1 LPToken trustline + ≤2 IOU/MPT
 *  pool-asset entries).  Counters track:
 *  - `nLPTokenTrustLines` — must be exactly 1 for `lpAccount`; any other
 *    LPToken trustline implies a second LP and returns `false` immediately.
 *  - `nIOUTrustLines` — IOU pool-asset trustlines (not LPToken); ≤2 each.
 *  - `nMPT` — MPToken objects for pool-side MPTs; ≤2.
 *  - `hasAMM` — exactly one `ltAMM` SLE must be present.
 *
 *  Final check: exactly 1 LPToken trustline and between 1–2 pool-asset
 *  entries (IOU + MPT combined); otherwise `tecINTERNAL`.
 *
 *  @return `true` if @p lpAccount is the only LP, `false` if other LPs exist,
 *      or `Unexpected(tecINTERNAL)` for any unexpected directory state.
 */
Expected<bool, TER>
isOnlyLiquidityProvider(ReadView const& view, Issue const& ammIssue, AccountID const& lpAccount)
{
    std::uint8_t nLPTokenTrustLines = 0;
    std::uint8_t nIOUTrustLines = 0;
    std::uint8_t nMPT = 0;
    bool hasAMM = false;
    std::uint8_t limit = 10;
    auto const root = keylet::ownerDir(ammIssue.account);
    auto currentIndex = root;

    while (limit-- >= 1)
    {
        auto const ownerDir = view.read(currentIndex);
        if (!ownerDir)
            return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
        for (auto const& key : ownerDir->getFieldV256(sfIndexes))
        {
            auto const sle = view.read(keylet::child(key));
            if (!sle)
                return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
            auto const entryType = sle->getFieldU16(sfLedgerEntryType);
            // Only one AMM object
            if (entryType == ltAMM)
            {
                if (hasAMM)
                    return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
                hasAMM = true;
                continue;
            }
            if (entryType == ltMPTOKEN)
            {
                ++nMPT;
                continue;
            }
            if (entryType != ltRIPPLE_STATE)
                return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
            auto const lowLimit = sle->getFieldAmount(sfLowLimit);
            auto const highLimit = sle->getFieldAmount(sfHighLimit);
            auto const isLPTrustline =
                lowLimit.getIssuer() == lpAccount || highLimit.getIssuer() == lpAccount;
            auto const isLPTokenTrustline =
                lowLimit.asset() == ammIssue || highLimit.asset() == ammIssue;

            if (isLPTrustline)
            {
                if (isLPTokenTrustline)
                {
                    // LP has exactly one LPToken trustline
                    if (++nLPTokenTrustLines > 1)
                        return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
                }
                else if (++nIOUTrustLines > 2)
                {
                    return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
                }
            }
            // Another LP's LPToken trustline: more than one LP exists.
            else if (isLPTokenTrustline)
            {
                return false;
            }
            else if (++nIOUTrustLines > 2)
            {
                return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
            }
        }
        auto const uNodeNext = ownerDir->getFieldU64(sfIndexNext);
        if (uNodeNext == 0)
        {
            if (nLPTokenTrustLines != 1 || (nIOUTrustLines == 0 && nMPT == 0) ||
                (nIOUTrustLines > 2 || nMPT > 2) || (nIOUTrustLines + nMPT) > 2)
                return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
            return true;
        }
        currentIndex = keylet::page(root, uNodeNext);
    }
    return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
}

/** Reconcile the AMM's stored `sfLPTokenBalance` with the last LP's actual
 *  trustline balance.
 *
 *  Due to the 16-significant-digit limit of `STAmount`, the AMM's running
 *  `sfLPTokenBalance` may differ from the LP's trustline balance by a small
 *  rounding error.  This function:
 *  1. Confirms @p account is the only remaining LP via `isOnlyLiquidityProvider()`.
 *  2. If so, checks that the discrepancy is within 0.1% (tolerance `1e-3`).
 *  3. If within tolerance, updates `sfLPTokenBalance` to match @p lpTokens
 *     so the final withdrawal leaves the AMM in a fully consistent state.
 *
 *  @return `true` when the balance was reconciled or no adjustment was needed
 *      (other LPs exist), `Unexpected(tecAMM_INVALID_TOKENS)` if the
 *      discrepancy exceeds tolerance, or `Unexpected(tecINTERNAL)` on an
 *      unexpected directory error.
 */
Expected<bool, TER>
verifyAndAdjustLPTokenBalance(
    Sandbox& sb,
    STAmount const& lpTokens,
    std::shared_ptr<SLE>& ammSle,
    AccountID const& account)
{
    auto const res = isOnlyLiquidityProvider(sb, lpTokens.get<Issue>(), account);
    if (!res.has_value())
    {
        return Unexpected<TER>(res.error());
    }

    if (res.value())
    {
        if (withinRelativeDistance(
                lpTokens, ammSle->getFieldAmount(sfLPTokenBalance), Number{1, -3}))
        {
            ammSle->setFieldAmount(sfLPTokenBalance, lpTokens);
            sb.update(ammSle);
        }
        else
        {
            return Unexpected<TER>(tecAMM_INVALID_TOKENS);
        }
    }
    return true;
}

}  // namespace xrpl
