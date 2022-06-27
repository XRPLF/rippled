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
#include <ripple/app/tx/impl/AMMDeposit.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/STAccount.h>

namespace ripple {

TxConsequences
AMMDeposit::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{ctx.tx};
}

NotTEC
AMMDeposit::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureAMM))
        return temDISABLED;

    auto const ret = preflight1(ctx);
    if (!isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid flags.";
        return temINVALID_FLAG;
    }

    auto const asset1In = ctx.tx[~sfAsset1In];
    auto const asset2In = ctx.tx[~sfAsset2In];
    auto const ePrice = ctx.tx[~sfEPrice];
    auto const lpTokens = ctx.tx[~sfLPTokens];
    // Valid combinations are:
    //   LPTokens
    //   Asset1In
    //   Asset1In and Asset2In
    //   Asset1In and LPTokens
    //   Asset1In and EPrice
    if ((!lpTokens && !asset1In) || (lpTokens && (asset2In || ePrice)) ||
        (asset1In &&
         ((asset2In && (lpTokens || ePrice)) ||
          (ePrice && (asset2In || lpTokens)))))
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid combination of "
                               "deposit fields.";
        return temBAD_AMM_OPTIONS;
    }
    if (lpTokens && *lpTokens == beast::zero)
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid LPTokens";
        return temBAD_AMM_TOKENS;
    }
    else if (
        auto const res =
            validAmount(asset1In, (lpTokens.has_value() || ePrice.has_value())))
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid Asset1In";
        return *res;
    }
    else if (auto const res = validAmount(asset2In))
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid Asset2InAmount";
        return *res;
    }
    else if (auto const res = validAmount(ePrice))
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid EPrice";
        return *res;
    }

    return preflight2(ctx);
}

TER
AMMDeposit::preclaim(PreclaimContext const& ctx)
{
    auto const ammSle = ctx.view.read(keylet::amm(ctx.tx[sfAMMHash]));
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: Invalid AMM account";
        return temBAD_SRC_ACCOUNT;
    }
    if (auto const ammRoot =
            ctx.view.read(keylet::account(ammSle->getAccountID(sfAMMAccount)));
        !ammRoot)
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: Invalid AMM account.";
        return temBAD_SRC_ACCOUNT;
    }
    auto const [asset1, asset2, lptAMMBalance] = getAMMBalances(
        ctx.view,
        ammSle->getAccountID(sfAMMAccount),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        ctx.j);
    if (asset1 <= beast::zero || asset2 <= beast::zero ||
        lptAMMBalance <= beast::zero)
    {
        JLOG(ctx.j.debug())
            << "AMM Deposit: reserves or tokens balance is zero";
        return tecAMM_BALANCE;
    }

    if (isFrozen(ctx.view, ctx.tx[~sfAsset1In]) ||
        isFrozen(ctx.view, ctx.tx[~sfAsset2Out]))
    {
        JLOG(ctx.j.debug()) << "AMM Deposit involves frozen asset";
        return tecFROZEN;
    }
    return tesSUCCESS;
}

void
AMMDeposit::preCompute()
{
    return Transactor::preCompute();
}

std::pair<TER, bool>
AMMDeposit::applyGuts(Sandbox& sb)
{
    auto const asset1In = ctx_.tx[~sfAsset1In];
    auto const asset2In = ctx_.tx[~sfAsset2In];
    auto const ePrice = ctx_.tx[~sfEPrice];
    auto const lpTokens = ctx_.tx[~sfLPTokens];
    auto const ammSle = ctx_.view().read(keylet::amm(ctx_.tx[sfAMMHash]));
    assert(ammSle);
    auto const ammAccountID = ammSle->getAccountID(sfAMMAccount);
    auto const [asset1, asset2, lptAMMBalance] = getAMMBalances(
        sb,
        ammAccountID,
        std::nullopt,
        asset1In ? asset1In->issue() : std::optional<Issue>{},
        asset2In ? asset2In->issue() : std::optional<Issue>{},
        ctx_.journal);

    auto const tfee = ammSle->getFieldU32(sfTradingFee);

    TER result = tesSUCCESS;

    if (asset1In)
    {
        if (asset2In)
            result = equalDepositLimit(
                sb,
                ammAccountID,
                asset1,
                asset2,
                lptAMMBalance,
                *asset1In,
                *asset2In);
        else if (lpTokens)
            result = singleDepositTokens(
                sb, ammAccountID, asset1, lptAMMBalance, *lpTokens, tfee);
        else if (ePrice)
            result = singleDepositEPrice(
                sb,
                ammAccountID,
                asset1,
                *asset1In,
                lptAMMBalance,
                *ePrice,
                tfee);
        else
            result = singleDeposit(
                sb, ammAccountID, asset1, lptAMMBalance, *asset1In, tfee);
    }
    else if (lpTokens)
        result = equalDepositTokens(
            sb, ammAccountID, asset1, asset2, lptAMMBalance, *lpTokens);

    return {result, result == tesSUCCESS};
}

TER
AMMDeposit::doApply()
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
AMMDeposit::deposit(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1,
    std::optional<STAmount> const& asset2,
    STAmount const& lpTokens)
{
    // Check account has sufficient funds
    auto balance = [&](auto const& asset) {
        return accountHolds(
                   view,
                   account_,
                   asset.issue().currency,
                   asset.issue().account,
                   FreezeHandling::fhZERO_IF_FROZEN,
                   ctx_.journal) >= asset;
    };

    // Deposit asset1
    if (!balance(asset1))
    {
        JLOG(ctx_.journal.debug())
            << "AMM Deposit: account has insufficient balance to deposit "
            << asset1;
        return tecUNFUNDED_AMM;
    }
    auto res = accountSend(view, account_, ammAccount, asset1, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Deposit: failed to deposit " << asset1;
        return res;
    }

    // Deposit asset2
    if (asset2)
    {
        if (!balance(*asset2))
        {
            JLOG(ctx_.journal.debug())
                << "AMM Deposit: account has insufficient balance to deposit "
                << *asset2;
            return tecUNFUNDED_AMM;
        }
        res = accountSend(view, account_, ammAccount, *asset2, ctx_.journal);
        if (res != tesSUCCESS)
        {
            JLOG(ctx_.journal.debug())
                << "AMM Deposit: failed to deposit " << *asset2;
            return res;
        }
    }

    // Deposit LP tokens
    res = accountSend(view, ammAccount, account_, lpTokens, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug()) << "AMM Deposit: failed to deposit LPTokens";
        return res;
    }

    return tesSUCCESS;
}

TER
AMMDeposit::equalDepositTokens(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& tokens)
{
    auto const frac = divide(tokens, lptAMMBalance, lptAMMBalance.issue());
    return deposit(
        view,
        ammAccount,
        multiply(asset1Balance, frac, asset1Balance.issue()),
        multiply(asset2Balance, frac, asset2Balance.issue()),
        tokens);
}

TER
AMMDeposit::equalDepositLimit(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& asset1In,
    STAmount const& asset2In)
{
    auto frac = Number{asset1In} / asset1Balance;
    auto tokens = toSTAmount(lptAMMBalance.issue(), lptAMMBalance * frac);
    if (tokens == beast::zero)
        return tecAMM_INVALID_TOKENS;
    auto const asset2Deposit = asset2Balance * frac;
    if (asset2Deposit <= asset2In)
        return deposit(
            view,
            ammAccount,
            asset1In,
            toSTAmount(asset2Balance.issue(), asset2Deposit),
            tokens);
    frac = Number{asset2In} / asset2Balance;
    tokens = toSTAmount(lptAMMBalance.issue(), lptAMMBalance * frac);
    if (tokens == beast::zero)
        return tecAMM_INVALID_TOKENS;
    auto const asset1Deposit = asset1Balance * frac;
    if (asset1Deposit <= asset1In)
        return deposit(
            view,
            ammAccount,
            toSTAmount(asset1Balance.issue(), asset1Deposit),
            asset2In,
            tokens);
    return tecAMM_FAILED_DEPOSIT;
}

TER
AMMDeposit::singleDeposit(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& lptAMMBalance,
    STAmount const& asset1In,
    std::uint16_t tfee)
{
    auto const tokens =
        calcLPTokensIn(asset1Balance, asset1In, lptAMMBalance, tfee);
    if (tokens == beast::zero)
        return tecAMM_INVALID_TOKENS;
    return deposit(view, ammAccount, asset1In, std::nullopt, tokens);
}

TER
AMMDeposit::singleDepositTokens(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& lptAMMBalance,
    STAmount const& tokens,
    std::uint16_t tfee)
{
    auto const asset1Deposit =
        calcAssetIn(asset1Balance, tokens, lptAMMBalance, tfee);
    return deposit(view, ammAccount, asset1Deposit, std::nullopt, tokens);
}

TER
AMMDeposit::singleDepositEPrice(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset1In,
    STAmount const& lptAMMBalance,
    STAmount const& ePrice,
    std::uint16_t tfee)
{
    if (asset1In != beast::zero)
    {
        auto const tokens =
            calcLPTokensIn(asset1Balance, asset1In, lptAMMBalance, tfee);
        if (tokens == beast::zero)
            return tecAMM_FAILED_DEPOSIT;
        auto const ep = Number{asset1In} / tokens;
        if (ep <= ePrice)
            return deposit(view, ammAccount, asset1In, std::nullopt, tokens);
    }

    auto const asset1In_ = toSTAmount(
        asset1Balance.issue(),
        power(ePrice * lptAMMBalance, 2) * feeMultHalf(tfee) / asset1Balance -
            2 * ePrice * lptAMMBalance);
    auto const tokens = toSTAmount(lptAMMBalance.issue(), asset1In_ / ePrice);
    return deposit(view, ammAccount, asset1In_, std::nullopt, tokens);
}

}  // namespace ripple