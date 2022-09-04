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

#include <ripple/app/misc/AMM.h>
#include <ripple/app/misc/AMM_formulae.h>
#include <ripple/app/tx/impl/AMMWithdraw.h>
#include <ripple/basics/Number.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/STAccount.h>

namespace ripple {

TxConsequences
AMMWithdraw::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{ctx.tx};
}

NotTEC
AMMWithdraw::preflight(PreflightContext const& ctx)
{
    if (!ammRequiredAmendments(ctx.rules))
        return temDISABLED;

    auto const ret = preflight1(ctx);
    if (!isTesSuccess(ret))
        return ret;

    auto const uFlags = ctx.tx.getFlags();
    if (uFlags & tfAMMWithdrawMask)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid flags.";
        return temINVALID_FLAG;
    }
    bool const withdrawAll = uFlags & tfAMMWithdrawAll;

    auto const asset1Out = ctx.tx[~sfAsset1Out];
    auto const asset2Out = ctx.tx[~sfAsset2Out];
    auto const ePrice = ctx.tx[~sfEPrice];
    auto const lpTokens = ctx.tx[~sfLPToken];
    // Valid combinations are:
    //   LPTokens|tfAMMWithdrawAll
    //   Asset1Out
    //   Asset1Out and Asset2Out
    //   Asset1Out and [LPTokens|tfAMMWithdrawAll]
    //   Asset1Out and EPrice
    if ((asset1Out && asset2Out && (lpTokens || withdrawAll || ePrice)) ||
        (asset1Out && (lpTokens || withdrawAll) && (asset2Out || ePrice)) ||
        (asset1Out && ePrice && (asset2Out || lpTokens || withdrawAll)) ||
        (asset2Out && !asset1Out) || (ePrice && !asset1Out) ||
        (!asset1Out && !lpTokens && !withdrawAll) ||
        (lpTokens && withdrawAll) ||
        (asset1Out && withdrawAll && *asset1Out != beast::zero))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid combination of "
                               "withdraw fields.";
        return temBAD_AMM_OPTIONS;
    }

    if (lpTokens && *lpTokens == beast::zero)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid tokens.";
        return temBAD_AMM_TOKENS;
    }

    if (auto const res =
            invalidAmount(asset1Out, withdrawAll || lpTokens || ePrice))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid Asset1Out";
        return *res;
    }

    if (auto const res = invalidAmount(asset2Out))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid Asset2OutAmount";
        return *res;
    }

    if (auto const res = invalidAmount(ePrice))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid EPrice";
        return *res;
    }

    return preflight2(ctx);
}

TER
AMMWithdraw::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];
    if (!ctx.view.read(keylet::account(accountID)))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: Invalid account.";
        return terNO_ACCOUNT;
    }

    auto const ammSle = getAMMSle(ctx.view, ctx.tx[sfAMMID]);
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: Invalid AMM account.";
        return terNO_ACCOUNT;
    }

    auto const asset1Out = ctx.tx[~sfAsset1Out];
    auto const asset2Out = ctx.tx[~sfAsset2Out];
    auto const ammAccountID = ammSle->getAccountID(sfAMMAccount);

    if ((asset1Out && requireAuth(ctx.view, asset1Out->issue(), accountID)) ||
        (asset2Out && requireAuth(ctx.view, asset2Out->issue(), accountID)))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: account is not authorized";
        return tecNO_PERMISSION;
    }

    if (isFrozen(ctx.view, asset1Out) || isFrozen(ctx.view, sfAsset2Out))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw involves frozen asset.";
        return tecFROZEN;
    }

    auto const lptBalance =
        lpHolds(ctx.view, ammAccountID, ctx.tx[sfAccount], ctx.j);
    auto const lpTokens = getTxLPTokens(ctx.view, ammAccountID, ctx.tx, ctx.j);

    if (lptBalance <= beast::zero)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: tokens balance is zero.";
        return tecAMM_BALANCE;
    }

    if (lpTokens && *lpTokens > lptBalance)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid tokens.";
        return tecAMM_INVALID_TOKENS;
    }

    if (lpTokens && lpTokens->issue() != lptBalance.issue())
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid LPTokens.";
        return temBAD_AMM_TOKENS;
    }

    return tesSUCCESS;
}

void
AMMWithdraw::preCompute()
{
    return Transactor::preCompute();
}

std::pair<TER, bool>
AMMWithdraw::applyGuts(Sandbox& sb)
{
    auto const asset1Out = ctx_.tx[~sfAsset1Out];
    auto const asset2Out = ctx_.tx[~sfAsset2Out];
    auto const ePrice = ctx_.tx[~sfEPrice];
    auto ammSle = getAMMSle(sb, ctx_.tx[sfAMMID]);
    assert(ammSle);
    auto const ammAccountID = ammSle->getAccountID(sfAMMAccount);
    auto const lpTokensWithdraw =
        getTxLPTokens(ctx_.view(), ammAccountID, ctx_.tx, ctx_.journal);

    auto const tfee = getTradingFee(*ammSle, account_);

    auto const [asset1, asset2, lptAMMBalance] = ammHolds(
        sb,
        *ammSle,
        asset1Out ? asset1Out->issue() : std::optional<Issue>{},
        asset2Out ? asset2Out->issue() : std::optional<Issue>{},
        ctx_.journal);

    auto const [result, withdrawnTokens] =
        [&,
         asset1 = std::ref(asset1),
         asset2 = std::ref(asset2),
         lptAMMBalance =
             std::ref(lptAMMBalance)]() -> std::pair<TER, STAmount> {
        if (asset1Out)
        {
            if (asset2Out)
                return equalWithdrawalLimit(
                    sb,
                    ammAccountID,
                    asset1,
                    asset2,
                    lptAMMBalance,
                    *asset1Out,
                    *asset2Out);
            else if (lpTokensWithdraw)
                return singleWithdrawalTokens(
                    sb,
                    ammAccountID,
                    asset1,
                    lptAMMBalance,
                    *asset1Out,
                    *lpTokensWithdraw,
                    tfee);
            else if (ePrice)
                return singleWithdrawalEPrice(
                    sb,
                    ammAccountID,
                    asset1,
                    lptAMMBalance,
                    *asset1Out,
                    *ePrice,
                    tfee);
            else
                return singleWithdrawal(
                    sb, ammAccountID, asset1, lptAMMBalance, *asset1Out, tfee);
        }
        else if (lpTokensWithdraw)
            return equalWithdrawalTokens(
                sb,
                ammAccountID,
                asset1,
                asset2,
                lptAMMBalance,
                *lpTokensWithdraw);
        // should not happen.
        JLOG(j_.error()) << "AMM Withdraw: invalid options.";
        return std::make_pair(tecAMM_FAILED_WITHDRAW, STAmount{});
    }();

    if (result == tesSUCCESS && withdrawnTokens != beast::zero)
    {
        ammSle->setFieldAmount(
            sfLPTokenBalance, lptAMMBalance - withdrawnTokens);
        sb.update(ammSle);
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
AMMWithdraw::deleteAccount(Sandbox& view, AccountID const& ammAccountID)
{
    auto sleAMMRoot = view.peek(keylet::account(ammAccountID));
    assert(sleAMMRoot);
    auto sleAMM = view.peek(keylet::amm(ctx_.tx[sfAMMID]));
    assert(sleAMM);

    if (!sleAMMRoot || !sleAMM)
        return tefBAD_LEDGER;

    // Note, the AMM trust lines are deleted since the balance
    // goes to 0. It also means there are no linked
    // ledger objects.
    view.erase(sleAMM);
    view.erase(sleAMMRoot);

    return tesSUCCESS;
}

std::pair<TER, STAmount>
AMMWithdraw::withdraw(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Withdraw,
    std::optional<STAmount> const& asset2Withdraw,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokensWithdraw)
{
    auto const ammSle = getAMMSle(view, ctx_.tx[sfAMMID]);
    assert(ammSle);
    auto const lpTokens = lpHolds(view, ammAccount, account_, ctx_.journal);
    auto const [issue1, issue2] = getTokensIssue(*ammSle);
    auto const [asset1, asset2] =
        ammPoolHolds(view, ammAccount, issue1, issue2, j_);

    // Invalid tokens or withdrawing more than own.
    if (lpTokensWithdraw == beast::zero || lpTokensWithdraw > lpTokens)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Withdraw: failed to withdraw, invalid LP tokens "
            << " tokens: " << lpTokensWithdraw << " " << lpTokens;
        return {tecAMM_INVALID_TOKENS, STAmount{}};
    }
    // Withdrawing all tokens but balances are not 0.
    if (lpTokensWithdraw == lptAMMBalance && asset1Withdraw != asset1 &&
        (!asset2Withdraw || *asset2Withdraw != asset2))
    {
        JLOG(ctx_.journal.debug())
            << "AMM Withdraw: failed to withdraw, invalid LP balance "
            << " asset1: " << asset1 << " " << asset1Withdraw
            << " asset2: " << asset2
            << (asset2Withdraw ? to_string(*asset2Withdraw) : "");
        return {tecAMM_BALANCE, STAmount{}};
    }

    // Withdraw asset1Withdraw
    auto res =
        ammSend(view, ammAccount, account_, asset1Withdraw, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Withdraw: failed to withdraw " << asset1Withdraw;
        return {res, STAmount{}};
    }

    // Withdraw asset2Withdraw
    if (asset2Withdraw)
    {
        res =
            ammSend(view, ammAccount, account_, *asset2Withdraw, ctx_.journal);
        if (res != tesSUCCESS)
        {
            JLOG(ctx_.journal.debug())
                << "AMM Withdraw: failed to withdraw " << *asset2Withdraw;
            return {res, STAmount{}};
        }
    }

    // Withdraw LP tokens
    res = redeemIOU(
        view,
        account_,
        lpTokensWithdraw,
        lpTokensWithdraw.issue(),
        ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Withdraw: failed to withdraw LPTokens";
        return {res, STAmount{}};
    }

    if (lpTokensWithdraw == lptAMMBalance)
        return {deleteAccount(view, ammAccount), STAmount{}};

    return {tesSUCCESS, lpTokensWithdraw};
}

/** Proportional withdrawal of pool assets for the amount of LPTokens.
 */
std::pair<TER, STAmount>
AMMWithdraw::equalWithdrawalTokens(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokensWithdraw)
{
    auto const frac = divide(lpTokensWithdraw, lptAMMBalance, noIssue());
    return withdraw(
        view,
        ammAccount,
        multiply(asset1Balance, frac, asset1Balance.issue()),
        multiply(asset2Balance, frac, asset2Balance.issue()),
        lptAMMBalance,
        lpTokensWithdraw);
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
AMMWithdraw::equalWithdrawalLimit(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& asset1Out,
    STAmount const& asset2Out)
{
    auto frac = Number{asset1Out} / asset1Balance;
    auto const asset2Withdraw = asset2Balance * frac;
    if (asset2Withdraw <= asset2Out)
        return withdraw(
            view,
            ammAccount,
            asset1Out,
            toSTAmount(asset2Out.issue(), asset2Withdraw),
            lptAMMBalance,
            toSTAmount(lptAMMBalance.issue(), lptAMMBalance * frac));
    frac = Number{asset2Out} / asset2Balance;
    auto const asset1Withdraw = asset1Balance * frac;
    return withdraw(
        view,
        ammAccount,
        toSTAmount(asset1Out.issue(), asset1Withdraw),
        asset2Out,
        lptAMMBalance,
        toSTAmount(lptAMMBalance.issue(), lptAMMBalance * frac));
}

/** Withdrawal of single asset equivalent to the amount specified in Asset1Out.
 *       t = T * (1 - sqrt(1 - b/(B * (1 - 0.5 * tfee)))) (7)
 * Use equation 7 to compute the t, given the amount in Asset1Out.
 */
std::pair<TER, STAmount>
AMMWithdraw::singleWithdrawal(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& lptAMMBalance,
    STAmount const& asset1Out,
    std::uint16_t tfee)
{
    auto const tokens =
        calcLPTokensOut(asset1Balance, asset1Out, lptAMMBalance, tfee);
    if (tokens == beast::zero)
        return {tecAMM_FAILED_WITHDRAW, STAmount{}};
    return withdraw(
        view, ammAccount, asset1Out, std::nullopt, lptAMMBalance, tokens);
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
AMMWithdraw::singleWithdrawalTokens(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& lptAMMBalance,
    STAmount const& asset1Out,
    STAmount const& lpTokensWithdraw,
    std::uint16_t tfee)
{
    auto const asset1Withdraw = calcWithdrawalByTokens(
        asset1Balance, lptAMMBalance, lpTokensWithdraw, tfee);
    if (asset1Out == beast::zero || asset1Withdraw >= asset1Out)
        return withdraw(
            view,
            ammAccount,
            toSTAmount(asset1Out.issue(), asset1Withdraw),
            std::nullopt,
            lptAMMBalance,
            lpTokensWithdraw);
    return {tecAMM_FAILED_WITHDRAW, STAmount{}};
}

/** Withdrawal of single asset with two constraints.
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
AMMWithdraw::singleWithdrawalEPrice(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& lptAMMBalance,
    STAmount const& asset1Out,
    STAmount const& ePrice,
    std::uint16_t tfee)
{
    auto const tokens = lptAMMBalance *
        (Number(2) -
         lptAMMBalance / (asset1Balance * ePrice * feeMultHalf(tfee)));
    if (tokens <= 0)
        return {tecAMM_FAILED_WITHDRAW, STAmount{}};
    auto const asset1Out_ = toSTAmount(asset1Out.issue(), tokens / ePrice);
    if (asset1Out == beast::zero ||
        (asset1Out != beast::zero && asset1Out_ >= asset1Out))
        return withdraw(
            view,
            ammAccount,
            asset1Out_,
            std::nullopt,
            lptAMMBalance,
            toSTAmount(lptAMMBalance.issue(), tokens));

    return {tecAMM_FAILED_WITHDRAW, STAmount{}};
}

std::optional<STAmount>
AMMWithdraw::getTxLPTokens(
    ReadView const& view,
    AccountID const& ammAccount,
    STTx const& tx,
    beast::Journal const journal)
{
    // withdraw all tokens - get the balance
    if (tx.getFlags() & tfAMMWithdrawAll)
        return lpHolds(view, ammAccount, tx[sfAccount], journal);
    else
        return tx[~sfLPToken];
}

}  // namespace ripple