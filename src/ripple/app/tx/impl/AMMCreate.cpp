//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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
#include <ripple/app/tx/impl/AMMCreate.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

TxConsequences
AMMCreate::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{ctx.tx};
}

NotTEC
AMMCreate::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureAMM))
        return temDISABLED;

    auto const ret = preflight1(ctx);
    if (!isTesSuccess(ret))
        return ret;

    auto& tx = ctx.tx;
    auto& j = ctx.j;

    if (tx.getFlags() & tfUniversalMask)
    {
        JLOG(j.debug()) << "AMM Instance: invalid flags.";
        return temINVALID_FLAG;
    }

    auto const saAsset1 = tx[sfAsset1];
    auto const saAsset2 = tx[sfAsset2];
    if (saAsset1.issue() == saAsset2.issue())
    {
        JLOG(j.debug()) << "AMM Instance: assets can not have the same issue.";
        return temBAD_AMM;
    }
    if (saAsset1 <= beast::zero || saAsset2 <= beast::zero)
    {
        JLOG(j.debug()) << "AMM Instance: bad amount.";
        return temBAD_AMOUNT;
    }

    // We don't allow a non-native currency to use the currency code XRP.
    if (badCurrency() == saAsset1.getCurrency() ||
        badCurrency() == saAsset2.getCurrency())
    {
        JLOG(j.debug()) << "AMM Instance: bad currency.";
        return temBAD_CURRENCY;
    }

    if ((saAsset1.native() && saAsset1.native() != !saAsset1.getIssuer()) ||
        (saAsset2.native() && saAsset2.native() != !saAsset2.getIssuer()))
    {
        JLOG(j.debug()) << "AMM Instance: bad issuer.";
        return temBAD_ISSUER;
    }

    if (tx[sfTradingFee] > 70000)
    {
        JLOG(j.debug()) << "AMM Instance: invalid trading fee.";
        return temBAD_FEE;
    }

    return preflight2(ctx);
}

TER
AMMCreate::preclaim(PreclaimContext const& ctx)
{
    auto& tx = ctx.tx;
    auto& j = ctx.j;
    auto const accountID = tx[sfAccount];
    auto const saAsset1 = tx[sfAsset1];
    auto const saAsset2 = tx[sfAsset2];

    if (requireAuth(ctx.view, saAsset1.issue(), accountID) ||
        requireAuth(ctx.view, saAsset2.issue(), accountID))
    {
        JLOG(j.debug()) << "AMM Instance: account is not authorized";
        return tecNO_PERMISSION;
    }

    if ((!saAsset1.native() &&
         isGlobalFrozen(ctx.view, saAsset1.getIssuer())) ||
        (!saAsset2.native() && isGlobalFrozen(ctx.view, saAsset2.getIssuer())))
    {
        JLOG(j.debug()) << "AMM Instance: involves frozen asset";
        return tecFROZEN;
    }

    auto const issue1Balance = accountHolds(
        ctx.view,
        accountID,
        saAsset1.issue().currency,
        saAsset1.issue().account,
        FreezeHandling::fhZERO_IF_FROZEN,
        ctx.j);
    auto const issue2Balance = accountHolds(
        ctx.view,
        accountID,
        saAsset2.issue().currency,
        saAsset2.issue().account,
        FreezeHandling::fhZERO_IF_FROZEN,
        ctx.j);
    if (issue1Balance < saAsset1 || issue2Balance < saAsset2)
    {
        JLOG(j.debug()) << "AMM Instance: has insufficient funds";
        return tecUNFUNDED_PAYMENT;
    }

    return tesSUCCESS;
}

void
AMMCreate::preCompute()
{
    return Transactor::preCompute();
}

std::pair<TER, bool>
AMMCreate::applyGuts(Sandbox& sb)
{
    auto const ammAccountID = ctx_.tx[sfAMMAccount];
    auto const saAsset1 = ctx_.tx[sfAsset1];
    auto const saAsset2 = ctx_.tx[sfAsset2];

    // AMM's creator account.
    auto const sleCreator = sb.peek(keylet::account(account_));
    if (!sleCreator)
        return {terNO_ACCOUNT, false};

    auto const ammHash = calcAMMGroupHash(saAsset1.issue(), saAsset2.issue());

    // AMM account already exists.
    if (sb.peek(keylet::account(ammAccountID)))
    {
        JLOG(j_.debug()) << "AMM Instance: AMM already exists.";
        return {tecAMM_EXISTS, false};
    }

    // LP Token already exists.
    auto const lptIssue = calcLPTIssue(ammAccountID);
    if (sb.read(keylet::line(ammAccountID, lptIssue)))
    {
        JLOG(j_.debug()) << "AMM Instance: LP Token already exists.";
        return {tecAMM_EXISTS, false};
    }

    // Check if ltAMM object exists
    if (auto ammSle = sb.peek(keylet::amm(ammHash)))
    {
        JLOG(j_.debug()) << "AMM Instance: ltAMM already exists.";
        return {tecAMM_EXISTS, false};
    }
    // Create ltAMM
    else
    {
        ammSle = std::make_shared<SLE>(keylet::amm(ammHash));
        ammSle->setFieldU32(sfTradingFee, ctx_.tx[sfTradingFee]);
        ammSle->setAccountID(sfAMMAccount, ammAccountID);
        STArray voteEntries;
        ammSle->setFieldArray(sfVoteEntries, voteEntries);
        sb.insert(ammSle);
    }

    // Create AMM Root Account.
    auto sleAMMRoot = std::make_shared<SLE>(keylet::account(ammAccountID));
    sleAMMRoot->setAccountID(sfAccount, ammAccountID);
    sleAMMRoot->setFieldAmount(sfBalance, STAmount{});
    std::uint32_t const seqno{
        view().rules().enabled(featureDeletableAccounts) ? view().seq() : 1};
    sleAMMRoot->setFieldU32(sfSequence, seqno);
    // Ignore reserves requirement and disable the master key.
    sleAMMRoot->setFieldU32(sfFlags, lsfAMM | lsfDisableMaster);
    sb.insert(sleAMMRoot);

    // Calculate initial LPT balance.
    auto const lpTokens = calcAMMLPT(saAsset1, saAsset2, lptIssue);
    // Send LPT to LP.
    auto res = accountSend(sb, ammAccountID, account_, lpTokens, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(j_.debug()) << "AMM Instance: failed to send LPT " << lpTokens;
        return {res, false};
    }

    // Send asset1.
    res = accountSend(sb, account_, ammAccountID, saAsset1, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(j_.debug()) << "AMM Instance: failed to send " << saAsset1;
        return {res, false};
    }

    // Send asset2.
    res = accountSend(sb, account_, ammAccountID, saAsset2, ctx_.journal);
    if (res != tesSUCCESS)
        JLOG(j_.debug()) << "AMM Instance: failed to send " << saAsset2;
    else
        JLOG(j_.debug()) << "AMM Instance: success " << ammAccountID << " "
                         << ammHash << " " << lptIssue << " " << saAsset1 << " "
                         << saAsset2;

    return {res, res == tesSUCCESS};
}

TER
AMMCreate::doApply()
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

}  // namespace ripple