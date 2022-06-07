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

    auto const asset1Out = ctx.tx[~sfAsset1Out];
    auto const asset2Out = ctx.tx[~sfAsset2Out];
    auto const ePrice = ctx.tx[~sfEPrice];
    auto const lpTokens = ctx.tx[~sfLPTokens];
    // Valid combinations are:
    //   LPTokens
    //   Asset1Out
    //   Asset1Out and Asset2Out
    //   Asset1Out and LPTokens
    //   Asset1Out and EPrice
    if ((!lpTokens && !asset1Out) || (lpTokens && (asset2Out || ePrice)) ||
        (asset1Out &&
         ((asset2Out && (lpTokens || ePrice)) ||
          (ePrice && (asset2Out || lpTokens)))))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid combination of "
                               "deposit fields.";
        return temBAD_AMM_OPTIONS;
    }
    if (lpTokens && *lpTokens == beast::zero)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: withdraw all tokens";
    }
    if (auto const res = validAmount(asset1Out, lpTokens.has_value()))
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
    auto const sleAMM = getAMMSle(ctx.view, ctx.tx[sfAMMHash]);
    if (!sleAMM)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: Invalid AMM account";
        return temBAD_SRC_ACCOUNT;
    }
    auto const asset1Out = ctx.tx[~sfAsset1Out];
    auto const asset2Out = ctx.tx[~sfAsset2Out];
    auto const ammAccountID = sleAMM->getAccountID(sfAMMAccount);
    auto const lptBalance =
        getLPTokens(ctx.view, ammAccountID, ctx.tx[sfAccount], ctx.j);
    auto const lpTokens = getTxLPTokens(ctx.view, ammAccountID, ctx.tx, ctx.j);
    if (lptBalance <= beast::zero)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: tokens balance is zero";
        return tecAMM_BALANCE;
    }
    if (lpTokens && *lpTokens > lptBalance)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid tokens";
        return tecAMM_INVALID_TOKENS;
    }
    if (isFrozen(ctx.view, asset1Out) || isFrozen(ctx.view, sfAsset2Out))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw involves frozen asset";
        return tecFROZEN;
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
    auto const sleAMM = ctx_.view().peek(keylet::amm(ctx_.tx[sfAMMHash]));
    assert(sleAMM);
    auto const ammAccountID = sleAMM->getAccountID(sfAMMAccount);
    auto const lpTokens =
        getTxLPTokens(ctx_.view(), ammAccountID, ctx_.tx, ctx_.journal);
    auto const [asset1, asset2, lptAMMBalance] = getAMMBalances(
        sb,
        ammAccountID,
        std::nullopt,
        asset1Out ? asset1Out->issue() : std::optional<Issue>{},
        asset2Out ? asset2Out->issue() : std::optional<Issue>{},
        ctx_.journal);

    auto const tfee = sleAMM->getFieldU32(sfTradingFee);
    auto const weight1 = orderWeight(
        sleAMM->getFieldU8(sfAssetWeight), asset1.issue(), asset2.issue());

    TER result = tesSUCCESS;

    if (asset1Out)
    {
        if (asset2Out)
            result = equalWithdrawalLimit(
                sb,
                ammAccountID,
                asset1,
                asset2,
                lptAMMBalance,
                *asset1Out,
                *asset2Out);
        else if (lpTokens)
            result = singleWithdrawalTokens(
                sb,
                ammAccountID,
                asset1,
                lptAMMBalance,
                *asset1Out,
                *lpTokens,
                weight1,
                tfee);
        else if (ePrice)
            result = singleWithdrawEPrice(
                sb,
                ammAccountID,
                asset1,
                asset2,
                lptAMMBalance,
                *asset1Out,
                *ePrice,
                weight1,
                tfee);
        else
            result = singleWithdrawal(
                sb,
                ammAccountID,
                asset1,
                lptAMMBalance,
                *asset1Out,
                weight1,
                tfee);
    }
    else if (lpTokens)
        result = equalWithdrawalTokens(
            sb, ammAccountID, asset1, asset2, lptAMMBalance, *lpTokens);
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

TER
AMMWithdraw::withdraw(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1,
    std::optional<STAmount> const& asset2,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens)
{
    auto const [lpAsset1, lpAsset2, lptAMM] = getAMMBalances(
        view, ammAccount, account_, asset1.issue(), std::nullopt, ctx_.journal);
    // The balances exceed LP holding or withdrawing all tokens and
    // there is some balance remaining.
    // TODO, needs work. Also see Swap balance validation
    if (lpTokens == beast::zero || lpTokens > lptAMM)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Withdraw: failed to withdraw, invalid LP tokens "
            << " tokens: " << lpTokens << " " << lptAMM;
        return tecAMM_INVALID_TOKENS;
    }
    if (lpTokens == lptAMMBalance && asset1 != lpAsset1 &&
        (!asset2.has_value() || *asset2 != lpAsset2))
    {
        JLOG(ctx_.journal.debug())
            << "AMM Withdraw: failed to withdraw, invalid LP balance "
            << " asset1: " << lpAsset1 << " " << asset1
            << " asset2: " << lpAsset2 << (asset2 ? to_string(*asset2) : "");
        return tecAMM_BALANCE;
    }

    // Withdraw asset1
    auto res = accountSend(view, ammAccount, account_, asset1, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Withdraw: failed to withdraw " << asset1;
        return res;
    }

    // Withdraw asset2
    if (asset2)
    {
        res = accountSend(view, ammAccount, account_, *asset2, ctx_.journal);
        if (res != tesSUCCESS)
        {
            JLOG(ctx_.journal.debug())
                << "AMM Withdraw: failed to withdraw " << *asset2;
            return res;
        }
    }

    // Withdraw LP tokens
    res = redeemIOU(view, account_, lpTokens, lpTokens.issue(), ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Withdraw: failed to withdraw LPTokens";
        return res;
    }

    if (lpTokens == lptAMMBalance)
        return deleteAccount(view, ammAccount);

    return tesSUCCESS;
}

TER
AMMWithdraw::equalWithdrawalTokens(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& tokens)
{
    auto const frac = divide(tokens, lptAMMBalance, noIssue());
    return withdraw(
        view,
        ammAccount,
        multiply(asset1Balance, frac, asset1Balance.issue()),
        multiply(asset2Balance, frac, asset2Balance.issue()),
        lptAMMBalance,
        tokens);
}

TER
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

TER
AMMWithdraw::singleWithdrawal(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& lptAMMBalance,
    STAmount const& asset1Out,
    std::uint8_t weight,
    std::uint16_t tfee)
{
    auto const tokens =
        calcLPTokensOut(asset1Balance, asset1Out, lptAMMBalance, weight, tfee);
    return withdraw(
        view, ammAccount, asset1Out, std::nullopt, lptAMMBalance, tokens);
}

TER
AMMWithdraw::singleWithdrawalTokens(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& lptAMMBalance,
    STAmount const& asset1Out,
    STAmount const& tokens,
    std::uint8_t weight,
    std::uint16_t tfee)
{
    auto const asset1Withdraw = calcWithdrawalByTokens(
        asset1Balance, lptAMMBalance, tokens, weight, tfee);
    if (asset1Out == beast::zero || asset1Withdraw >= asset1Out)
        return withdraw(
            view,
            ammAccount,
            toSTAmount(asset1Out.issue(), asset1Withdraw),
            std::nullopt,
            lptAMMBalance,
            tokens);
    return tecAMM_FAILED_WITHDRAW;
}

TER
AMMWithdraw::singleWithdrawEPrice(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& asset1Out,
    STAmount const& ePrice,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
#if 0
    auto const asset1BalanceUpd = asset1Balance - asset1Out;
    auto const sp =
        calcSpotPrice(asset1BalanceUpd, asset2Balance, weight1, tfee);
    auto const asset1Deposit = [&]() -> std::optional<STAmount> {
        if (sp <= STAmount{noIssue(), maxSP.mantissa(), maxSP.exponent()})
            return asset1Out;
        return changeSpotPrice(
            asset1Balance, asset2Balance, maxSP, weight1, tfee);
    }();
    if (!asset1Deposit)
        return tecAMM_FAILED_DEPOSIT;
    auto const tokens = calcLPTokensOut(
        asset1Balance, *asset1Deposit, lptAMMBalance, weight1, tfee);
    if (!tokens)
        return tecAMM_FAILED_DEPOSIT;
    return withdraw(
        view, ammAccount, *asset1Deposit, std::nullopt, lptAMMBalance, tokens);
#endif
    return tecAMM_FAILED_DEPOSIT;
}

std::optional<STAmount>
AMMWithdraw::getTxLPTokens(
    ReadView const& view,
    AccountID const& ammAccount,
    STTx const& tx,
    beast::Journal const journal)
{
    // special case - withdraw all tokens
    if (auto const tokens = tx[~sfLPTokens]; tokens && *tokens == beast::zero)
        return getLPTokens(view, ammAccount, tx[sfAccount], journal);
    else
        return tokens;
}

}  // namespace ripple