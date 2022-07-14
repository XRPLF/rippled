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
    if (!ctx.rules.enabled(featureAMM))
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
    auto const lpTokens = ctx.tx[~sfLPTokens];
    // Valid combinations are:
    //   LPTokens|tfAMMWithdrawAll
    //   Asset1Out
    //   Asset1Out and Asset2Out
    //   Asset1Out and [LPTokens|tfAMMWithdrawAll]
    //   Asset1Out and EPrice
    if ((!lpTokens && !asset1Out && !withdrawAll) ||
        (lpTokens && (asset2Out || ePrice || withdrawAll)) ||
        (asset1Out &&
         ((asset2Out && (lpTokens || ePrice || withdrawAll)) ||
          (ePrice && (asset2Out || lpTokens || withdrawAll)))) ||
        (ePrice && (asset2Out || lpTokens || withdrawAll)) ||
        (asset2Out && withdrawAll && *asset2Out != beast::zero))
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
    if (auto const res = validAmount(
            asset1Out,
            withdrawAll || lpTokens.has_value() || ePrice.has_value()))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid Asset1Out";
        return *res;
    }
    else if (auto const res = validAmount(asset2Out))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid Asset2OutAmount";
        return *res;
    }
    else if (auto const res = validAmount(ePrice))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid EPrice";
        return *res;
    }

    return preflight2(ctx);
}

TER
AMMWithdraw::preclaim(PreclaimContext const& ctx)
{
    if (!ctx.view.read(keylet::account(ctx.tx[sfAccount])))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: Invalid account.";
        return terNO_ACCOUNT;
    }

    auto const ammSle = getAMMSle(ctx.view, ctx.tx[sfAMMHash]);
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: Invalid AMM account.";
        return terNO_ACCOUNT;
    }

    auto const asset1Out = ctx.tx[~sfAsset1Out];
    auto const asset2Out = ctx.tx[~sfAsset2Out];
    auto const ammAccountID = ammSle->getAccountID(sfAMMAccount);

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
    auto ammSle = getAMMSle(sb, ctx_.tx[sfAMMHash]);
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
    auto sleAMM = view.peek(keylet::amm(ctx_.tx[sfAMMHash]));
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
    auto const ammSle = getAMMSle(view, ctx_.tx[sfAMMHash]);
    assert(ammSle);
    auto const lpTokens = lpHolds(view, ammAccount, account_, ctx_.journal);
    auto const [issue1, issue2] = getTokensIssue(*ammSle);
    auto const [asset1, asset2] =
        ammPoolHolds(view, ammAccount, issue1, issue2, j_);

    // TODO there should be constraints on what can be withdrawn.
    // For instance, all tokens can't be withdrawn from one pool.

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
        (!asset2Withdraw.has_value() || *asset2Withdraw != asset2))
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
        accountSend(view, ammAccount, account_, asset1Withdraw, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Withdraw: failed to withdraw " << asset1Withdraw;
        return {res, STAmount{}};
    }

    // Withdraw asset2Withdraw
    if (asset2Withdraw)
    {
        res = accountSend(
            view, ammAccount, account_, *asset2Withdraw, ctx_.journal);
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
    return withdraw(
        view, ammAccount, asset1Out, std::nullopt, lptAMMBalance, tokens);
}

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
        return tx[~sfLPTokens];
}

}  // namespace ripple