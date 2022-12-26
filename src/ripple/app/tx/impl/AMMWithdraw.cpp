//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2022 Ripple Labs Inc.

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

#include <ripple/app/tx/impl/AMMWithdraw.h>

#include <ripple/app/misc/AMM.h>
#include <ripple/app/misc/AMM_formulae.h>
#include <ripple/basics/Number.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/STAccount.h>

namespace ripple {

NotTEC
AMMWithdraw::preflight(PreflightContext const& ctx)
{
    if (!ammEnabled(ctx.rules))
        return temDISABLED;

    auto const ret = preflight1(ctx);
    if (!isTesSuccess(ret))
        return ret;

    auto const flags = ctx.tx.getFlags();
    if (flags & tfWithdrawMask)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid flags.";
        return temINVALID_FLAG;
    }

    auto const amount = ctx.tx[~sfAmount];
    auto const amount2 = ctx.tx[~sfAmount2];
    auto const ePrice = ctx.tx[~sfEPrice];
    auto const lpTokens = ctx.tx[~sfLPTokenIn];
    // Valid combinations are:
    //   LPTokens
    //   tfWithdrawAll
    //   Amount
    //   tfOneAssetWithdrawAll & Amount
    //   Amount and Amount2
    //   Amount and LPTokens
    //   Amount and EPrice
    if (auto const subTxType = std::bitset<32>(flags & tfWithdrawSubTx);
        subTxType.none() || subTxType.count() > 1)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid flags.";
        return temMALFORMED;
    }
    else if (flags & tfLPToken)
    {
        if (!lpTokens || amount || amount2 || ePrice)
            return temMALFORMED;
    }
    else if (flags & tfWithdrawAll)
    {
        if (lpTokens || amount || amount2 || ePrice)
            return temMALFORMED;
    }
    else if (flags & tfOneAssetWithdrawAll)
    {
        if (!amount || lpTokens || amount2 || ePrice)
            return temMALFORMED;
    }
    else if (flags & tfSingleAsset)
    {
        if (!amount || lpTokens || amount2 || ePrice)
            return temMALFORMED;
    }
    else if (flags & tfTwoAsset)
    {
        if (!amount || !amount2 || lpTokens || ePrice)
            return temMALFORMED;
    }
    else if (flags & tfOneAssetLPToken)
    {
        if (!amount || !lpTokens || amount2 || ePrice)
            return temMALFORMED;
    }
    else if (flags & tfLimitLPToken)
    {
        if (!amount || !ePrice || lpTokens || amount2)
            return temMALFORMED;
    }

    auto const asset = ctx.tx[sfAsset];
    auto const asset2 = ctx.tx[sfAsset2];
    if (auto const res = invalidAMMAssetPair(asset, asset2))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: Invalid asset pair.";
        return res;
    }

    if (amount && amount2 && amount->issue() == amount2->issue())
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid tokens, same issue."
                            << amount->issue() << " " << amount2->issue();
        return temAMM_BAD_TOKENS;
    }

    if (lpTokens && *lpTokens <= beast::zero)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid tokens.";
        return temAMM_BAD_TOKENS;
    }

    if (auto const res = invalidAMMAmount(
            amount,
            std::make_optional(std::make_pair(asset, asset2)),
            (flags & (tfOneAssetWithdrawAll | tfOneAssetLPToken)) || ePrice))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid Asset1Out";
        return res;
    }

    if (auto const res = invalidAMMAmount(
            amount2, std::make_optional(std::make_pair(asset, asset2))))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid Asset2OutAmount";
        return res;
    }

    if (auto const res = invalidAMMAmount(ePrice))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid EPrice";
        return res;
    }

    return preflight2(ctx);
}

static std::optional<STAmount>
tokensWithdraw(
    STAmount const& lpTokens,
    std::optional<STAmount> const& tokensIn,
    std::uint32_t flags)
{
    if (flags & (tfWithdrawAll | tfOneAssetWithdrawAll))
        return lpTokens;
    return tokensIn;
}

TER
AMMWithdraw::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];

    auto const ammSle = getAMMSle(ctx.view, ctx.tx[sfAsset], ctx.tx[sfAsset2]);
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: Invalid asset pair.";
        return terNO_AMM;
    }

    auto const amount = ctx.tx[~sfAmount];
    auto const amount2 = ctx.tx[~sfAmount2];

    auto const expected = ammHolds(
        ctx.view,
        **ammSle,
        amount ? amount->issue() : std::optional<Issue>{},
        amount2 ? amount2->issue() : std::optional<Issue>{},
        FreezeHandling::fhZERO_IF_FROZEN,
        ctx.j);
    if (!expected)
        return expected.error();
    auto const [amountBalance, amount2Balance, lptAMMBalance] = *expected;
    if (amountBalance <= beast::zero || amount2Balance <= beast::zero ||
        lptAMMBalance <= beast::zero)
    {
        if (isFrozen(ctx.view, amountBalance) ||
            isFrozen(ctx.view, amount2Balance))
        {
            JLOG(ctx.j.debug()) << "AMM Withdraw involves frozen asset.";
            return tecFROZEN;
        }
        JLOG(ctx.j.debug())
            << "AMM Withdraw: reserves or tokens balance is zero.";
        return tecAMM_BALANCE;
    }

    auto checkAmount = [&](auto const& amount, auto const& balance) -> TER {
        if (amount)
        {
            if (auto const ter =
                    requireAuth(ctx.view, amount->issue(), accountID))
            {
                JLOG(ctx.j.debug())
                    << "AMM Withdraw: account is not authorized, "
                    << amount->issue();
                return ter;
            }
            if (amount > balance)
            {
                JLOG(ctx.j.debug())
                    << "AMM Withdraw: withdrawing more than the balance, "
                    << *amount;
                return tecAMM_BALANCE;
            }
        }
        return tesSUCCESS;
    };

    if (auto const ter = checkAmount(amount, amountBalance))
        return ter;

    if (auto const ter = checkAmount(amount2, amount2Balance))
        return ter;

    auto const lpTokens =
        ammLPHolds(ctx.view, **ammSle, ctx.tx[sfAccount], ctx.j);
    auto const lpTokensWithdraw =
        tokensWithdraw(lpTokens, ctx.tx[~sfLPTokenIn], ctx.tx.getFlags());

    if (lpTokens <= beast::zero)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: tokens balance is zero.";
        return tecAMM_BALANCE;
    }

    if (lpTokensWithdraw && lpTokensWithdraw->issue() != lpTokens.issue())
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid LPTokens.";
        return temAMM_BAD_TOKENS;
    }

    if (lpTokensWithdraw && *lpTokensWithdraw > lpTokens)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid tokens.";
        return tecAMM_INVALID_TOKENS;
    }

    if (auto const ePrice = ctx.tx[~sfEPrice];
        ePrice && ePrice->issue() != lpTokens.issue())
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid EPrice.";
        return temAMM_BAD_TOKENS;
    }

    return tesSUCCESS;
}

std::pair<TER, bool>
AMMWithdraw::applyGuts(Sandbox& sb)
{
    auto const amount = ctx_.tx[~sfAmount];
    auto const amount2 = ctx_.tx[~sfAmount2];
    auto const ePrice = ctx_.tx[~sfEPrice];
    auto ammSle = getAMMSle(sb, ctx_.tx[sfAsset], ctx_.tx[sfAsset2]);
    if (!ammSle)
        return {ammSle.error(), false};
    auto const ammAccountID = (**ammSle)[sfAMMAccount];
    auto const lpTokens =
        ammLPHolds(ctx_.view(), **ammSle, ctx_.tx[sfAccount], ctx_.journal);
    auto const lpTokensWithdraw =
        tokensWithdraw(lpTokens, ctx_.tx[~sfLPTokenIn], ctx_.tx.getFlags());

    auto const tfee = getTradingFee(ctx_.view(), **ammSle, account_);

    auto const expected = ammHolds(
        sb,
        **ammSle,
        amount ? amount->issue() : std::optional<Issue>{},
        amount2 ? amount2->issue() : std::optional<Issue>{},
        FreezeHandling::fhZERO_IF_FROZEN,
        ctx_.journal);
    if (!expected)
        return {expected.error(), false};
    auto const [amountBalance, amount2Balance, lptAMMBalance] = *expected;

    auto const subTxType = ctx_.tx.getFlags() & tfWithdrawSubTx;

    auto const [result, newLPTokenBalance] =
        [&,
         &amountBalance = amountBalance,
         &amount2Balance = amount2Balance,
         &lptAMMBalance = lptAMMBalance]() -> std::pair<TER, STAmount> {
        if (subTxType & tfTwoAsset)
            return equalWithdrawLimit(
                sb,
                ammAccountID,
                amountBalance,
                amount2Balance,
                lptAMMBalance,
                *amount,
                *amount2);
        if (subTxType & tfOneAssetLPToken || subTxType & tfOneAssetWithdrawAll)
            return singleWithdrawTokens(
                sb,
                ammAccountID,
                amountBalance,
                lptAMMBalance,
                *amount,
                *lpTokensWithdraw,
                tfee);
        if (subTxType & tfLimitLPToken)
            return singleWithdrawEPrice(
                sb,
                ammAccountID,
                amountBalance,
                lptAMMBalance,
                *amount,
                *ePrice,
                tfee);
        if (subTxType & tfSingleAsset)
            return singleWithdraw(
                sb, ammAccountID, amountBalance, lptAMMBalance, *amount, tfee);
        if (subTxType & tfLPToken || subTxType & tfWithdrawAll)
            return equalWithdrawTokens(
                sb,
                ammAccountID,
                amountBalance,
                amount2Balance,
                lptAMMBalance,
                lpTokens,
                *lpTokensWithdraw);
        // should not happen.
        JLOG(j_.error()) << "AMM Withdraw: invalid options.";
        return std::make_pair(tecAMM_FAILED_WITHDRAW, STAmount{});
    }();

    // AMM is deleted if zero tokens balance
    if (result == tesSUCCESS && newLPTokenBalance != beast::zero)
    {
        (*ammSle)->setFieldAmount(sfLPTokenBalance, newLPTokenBalance);
        sb.update(*ammSle);

        JLOG(ctx_.journal.trace())
            << "AMM Withdraw: tokens " << to_string(newLPTokenBalance.iou())
            << " " << to_string(lpTokens.iou()) << " "
            << to_string(lptAMMBalance.iou());
    }

    return {result, result == tesSUCCESS};
}

TER
AMMWithdraw::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    // This is a ledger with just the fees paid and any unfunded or expired
    // offers we encounter removed. It's used when handling Fill-or-Kill offers,
    // if the order isn't going to be placed, to avoid wasting the work we did.
    Sandbox sbCancel(&ctx_.view());

    auto const result = applyGuts(sb);
    if (result.second)
        sb.apply(ctx_.rawView());
    else
        sbCancel.apply(ctx_.rawView());

    return result.first;
}

TER
AMMWithdraw::deleteAccount(Sandbox& sb, AccountID const& ammAccountID)
{
    auto sleAMMRoot = sb.peek(keylet::account(ammAccountID));
    auto sleAMM = getAMMSle(sb, ctx_.tx[sfAsset], ctx_.tx[sfAsset2]);

    if (!sleAMMRoot || !sleAMM)
        return tecINTERNAL;

    // Note, the AMM trust lines are deleted since the balance
    // goes to 0. It also means there are no linked
    // ledger objects.
    sb.erase(*sleAMM);
    sb.erase(sleAMMRoot);

    return tesSUCCESS;
}

std::pair<TER, STAmount>
AMMWithdraw::withdraw(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& amountWithdraw,
    std::optional<STAmount> const& amount2Withdraw,
    STAmount const& lpTokensAMMBalance,
    STAmount const& lpTokensWithdraw)
{
    auto const ammSle =
        getAMMSle(ctx_.view(), ctx_.tx[sfAsset], ctx_.tx[sfAsset2]);
    if (!ammSle)
        return {ammSle.error(), STAmount{}};
    auto const lpTokens = ammLPHolds(view, **ammSle, account_, ctx_.journal);
    auto const expected = ammHolds(
        view,
        **ammSle,
        amountWithdraw.issue(),
        std::nullopt,
        FreezeHandling::fhZERO_IF_FROZEN,
        j_);
    if (!expected)
        return {expected.error(), STAmount{}};
    auto const [curBalance, curBalance2, _] = *expected;
    (void)_;

    // Withdrawing one side of the pool
    if (amountWithdraw == curBalance && !amount2Withdraw)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Withdraw: failed to withdraw one side of the pool "
            << " curBalance: " << curBalance << " " << amountWithdraw
            << " lpTokensBalance: " << lpTokensWithdraw << " lptBalance "
            << lpTokensAMMBalance;
        return {tecAMM_FAILED_WITHDRAW, STAmount{}};
    }

    // Withdrawing more than the pool's balance
    if (amountWithdraw > curBalance || amount2Withdraw > curBalance2)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Withdraw: withdrawing more than the pool's balance "
            << " curBalance: " << curBalance << " " << amountWithdraw
            << " curBalance2: " << curBalance2 << " "
            << (amount2Withdraw ? *amount2Withdraw : STAmount{})
            << " lpTokensBalance: " << lpTokensWithdraw << " lptBalance "
            << lpTokensAMMBalance;
        return {tecAMM_FAILED_WITHDRAW, STAmount{}};
    }

    // Withdraw amountWithdraw
    auto res =
        ammSend(view, ammAccount, account_, amountWithdraw, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Withdraw: failed to withdraw " << amountWithdraw;
        return {res, STAmount{}};
    }

    // Withdraw amount2Withdraw
    if (amount2Withdraw)
    {
        res =
            ammSend(view, ammAccount, account_, *amount2Withdraw, ctx_.journal);
        if (res != tesSUCCESS)
        {
            JLOG(ctx_.journal.debug())
                << "AMM Withdraw: failed to withdraw " << *amount2Withdraw;
            return {res, STAmount{}};
        }
    }

    auto const actualTokensWithdraw = [&]() {
        if (!(ctx_.tx[sfFlags] & (tfWithdrawAll | tfOneAssetWithdrawAll)))
        {
            // New pool LPToken balance
            auto const newLPTokenBalance =
                lpTokensAMMBalance - lpTokensWithdraw;
            // Actual withdraw. Adjust for the loss of significant digit.
            return lpTokensAMMBalance - newLPTokenBalance;
        }
        return lpTokensWithdraw;
    }();
    if (actualTokensWithdraw <= beast::zero || actualTokensWithdraw > lpTokens)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Withdraw: failed to withdraw, invalid LP tokens "
            << " tokens: " << actualTokensWithdraw << " " << lpTokens << " "
            << lpTokensAMMBalance;
        return {tecAMM_FAILED_WITHDRAW, STAmount{}};
    }

    // Withdraw LP tokens
    res = redeemIOU(
        view,
        account_,
        actualTokensWithdraw,
        actualTokensWithdraw.issue(),
        ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Withdraw: failed to withdraw LPTokens";
        return {res, STAmount{}};
    }

    if (actualTokensWithdraw == lpTokensAMMBalance)
        return {deleteAccount(view, ammAccount), STAmount{}};

    return {tesSUCCESS, lpTokensAMMBalance - actualTokensWithdraw};
}

/** Proportional withdrawal of pool assets for the amount of LPTokens.
 */
std::pair<TER, STAmount>
AMMWithdraw::equalWithdrawTokens(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& amount2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens,
    STAmount const& lpTokensWithdraw)
{
    try
    {
        // Withdrawing all tokens in the pool
        if ((ctx_.tx[sfFlags] & tfWithdrawAll) &&
            lpTokensWithdraw == lptAMMBalance)
            return withdraw(
                view,
                ammAccount,
                amountBalance,
                amount2Balance,
                lptAMMBalance,
                lpTokensWithdraw);
        auto const frac = divide(lpTokensWithdraw, lptAMMBalance, noIssue());
        auto getAmounts = [&](auto const& frac) {
            return std::make_pair(
                multiply(amountBalance, frac, amountBalance.issue()),
                multiply(amount2Balance, frac, amount2Balance.issue()));
        };
        auto const [withdrawAmount, withdraw2Amount] = getAmounts(frac);
        // The amount of requested tokens to withdraw results
        // in one-sided pool withdrawal
        if (withdrawAmount == beast::zero || withdraw2Amount == beast::zero)
        {
            // LP can request more tokens to withdraw, which doesn't result
            // in single pool withdrawal
            if (lpTokensWithdraw < lpTokens)
            {
                auto const frac = divide(lpTokens, lptAMMBalance, noIssue());
                auto const [withdrawAmount, withdraw2Amount] = getAmounts(frac);
                if (withdrawAmount != beast::zero &&
                    withdraw2Amount != beast::zero)
                    return {tecAMM_FAILED_WITHDRAW, STAmount{}};
                // Total LP tokens withdrawal still result in single pool
                // withdrawal
            }
            // LP withdrawing all tokens, treat as the single tokens withdraw
            // with no fee
            auto const amount = withdrawAmount != beast::zero ? withdrawAmount
                                                              : withdraw2Amount;
            auto const balance = amount.issue() == amountBalance.issue()
                ? amountBalance
                : amount2Balance;
            return singleWithdrawTokens(
                view,
                ammAccount,
                balance,
                lptAMMBalance,
                amount,
                lpTokensWithdraw,
                0);
        }
        return withdraw(
            view,
            ammAccount,
            withdrawAmount,
            withdraw2Amount,
            lptAMMBalance,
            lpTokensWithdraw);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "AMMWithdraw::equalWithdrawTokens exception "
                         << e.what();
    }
    return {tecINTERNAL, STAmount{}};
}

/** All assets withdrawal with the constraints on the maximum amount
 * of each asset that the trader is willing to withdraw.
 *       a = (t/T) * A (5)
 *       b = (t/T) * B (6)
 *       where
 *      A,B: current pool composition
 *      T: current balance of outstanding LPTokens
 *      a: balance of asset A being added
 *      b: balance of asset B being added
 *      t: balance of LPTokens issued to LP after a successful transaction
 * Use equation 5 to compute , given the amount in Asset1Out. Let this be Z
 * Use equation 6 to compute the amount of asset2, given  t~Z. Let
 *     the computed amount of asset2 be X
 * If X <= amount in Asset2Out:
 *   The amount of asset1 to be withdrawn is the one specified in Asset1Out
 *   The amount of asset2 to be withdrawn is X
 *   The amount of LPTokens redeemed is Z
 * If X> amount in Asset2Out:
 *   Use equation 5 to compute , given the amount in Asset2Out. Let this be Q
 *   Use equation 6 to compute the amount of asset1, given t~Q.
 *     Let the computed amount of asset1 be W
 *   The amount of asset2 to be withdrawn is the one specified in Asset2Out
 *   The amount of asset1 to be withdrawn is W
 *   The amount of LPTokens redeemed is Q
 */
std::pair<TER, STAmount>
AMMWithdraw::equalWithdrawLimit(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& amount2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& amount,
    STAmount const& amount2)
{
    auto frac = Number{amount} / amountBalance;
    auto const amount2Withdraw = amount2Balance * frac;
    if (amount2Withdraw <= amount2)
        return withdraw(
            view,
            ammAccount,
            amount,
            toSTAmount(amount2.issue(), amount2Withdraw),
            lptAMMBalance,
            toSTAmount(lptAMMBalance.issue(), lptAMMBalance * frac));
    frac = Number{amount2} / amount2Balance;
    auto const amountWithdraw = amountBalance * frac;
    return withdraw(
        view,
        ammAccount,
        toSTAmount(amount.issue(), amountWithdraw),
        amount2,
        lptAMMBalance,
        toSTAmount(lptAMMBalance.issue(), lptAMMBalance * frac));
}

/** Withdraw single asset equivalent to the amount specified in Asset1Out.
 *       t = T * (1 - sqrt(1 - b/(B * (1 - 0.5 * tfee)))) (7)
 * Use equation 7 to compute the t, given the amount in Asset1Out.
 */
std::pair<TER, STAmount>
AMMWithdraw::singleWithdraw(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& lptAMMBalance,
    STAmount const& amount,
    std::uint16_t tfee)
{
    auto const tokens = lpTokensOut(amountBalance, amount, lptAMMBalance, tfee);
    if (tokens == beast::zero)
        return {tecAMM_FAILED_WITHDRAW, STAmount{}};
    return withdraw(
        view, ammAccount, amount, std::nullopt, lptAMMBalance, tokens);
}

/** withdrawal of single asset specified in Asset1Out proportional
 * to the share represented by the amount of LPTokens.
 *       Y = B * (1 - (1 - t/T)**2) * (1 - 0.5 * tfee) (8)
 * Use equation 8 to compute the amount of asset1, given the redeemed t
 *   represented by LPTokens. Let this be Y.
 * If (amount exists for Asset1Out & Y >= amount in Asset1Out) ||
 *       (amount field does not exist for Asset1Out):
 *   The amount of asset out is Y
 *   The amount of LPTokens redeemed is LPTokens
 */
std::pair<TER, STAmount>
AMMWithdraw::singleWithdrawTokens(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& lptAMMBalance,
    STAmount const& amount,
    STAmount const& lpTokensWithdraw,
    std::uint16_t tfee)
{
    auto const amountWithdraw =
        withdrawByTokens(amountBalance, lptAMMBalance, lpTokensWithdraw, tfee);
    if (amount == beast::zero || amountWithdraw >= amount)
        return withdraw(
            view,
            ammAccount,
            amountWithdraw,
            std::nullopt,
            lptAMMBalance,
            lpTokensWithdraw);
    return {tecAMM_FAILED_WITHDRAW, STAmount{}};
}

/** Withdraw single asset with two constraints.
 * a. amount of asset1 if specified in Asset1Out specifies the minimum
 *     amount of asset1 that the trader is willing to withdraw.
 * b. The effective price of asset traded out does not exceed the amount
 *     specified in EPrice
 *       The effective price (EP) of a trade is defined as the ratio
 *       of the tokens the trader sold or swapped in (Token B) and
 *       the token they got in return or swapped out (Token A).
 *       EP(B/A) = b/a (III)
 *       b = B * (1 - (1 - t/T)**2) * (1 - 0.5 * tfee) (8)
 * Use equations 8 & III and amount in EPrice to compute the two variables:
 *   asset in as LPTokens. Let this be X
 *   asset out as that in Asset1Out. Let this be Y
 * If (amount exists for Asset1Out & Y >= amount in Asset1Out) ||
 *     (amount field does not exist for Asset1Out):
 *   The amount of assetOut is given by Y
 *   The amount of LPTokens is given by X
 */
std::pair<TER, STAmount>
AMMWithdraw::singleWithdrawEPrice(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& lptAMMBalance,
    STAmount const& amount,
    STAmount const& ePrice,
    std::uint16_t tfee)
{
    auto const tokens = lptAMMBalance *
        (Number(2) -
         lptAMMBalance / (amountBalance * ePrice * feeMultHalf(tfee)));
    if (tokens <= 0)
        return {tecAMM_FAILED_WITHDRAW, STAmount{}};
    auto const amountWithdraw = toSTAmount(amount.issue(), tokens / ePrice);
    if (amount == beast::zero ||
        (amount != beast::zero && amountWithdraw >= amount))
        return withdraw(
            view,
            ammAccount,
            amountWithdraw,
            std::nullopt,
            lptAMMBalance,
            toSTAmount(lptAMMBalance.issue(), tokens));

    return {tecAMM_FAILED_WITHDRAW, STAmount{}};
}

}  // namespace ripple
