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
        JLOG(j.debug())
            << "AMM Instance: tokens can not have the same currency/issuer.";
        return temBAD_AMM_TOKENS;
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

    if (tx[sfTradingFee] > 65000)
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

    if (!ctx.view.read(keylet::account(accountID)))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: Invalid account.";
        return terNO_ACCOUNT;
    }

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

    auto insufficientBalance = [&](STAmount const& asset) {
        auto const balance = accountHolds(
            ctx.view,
            accountID,
            asset.issue().currency,
            asset.issue().account,
            FreezeHandling::fhZERO_IF_FROZEN,
            ctx.j);
        return balance < asset;
    };

    if (insufficientBalance(saAsset1) || insufficientBalance(saAsset2))
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
    auto const saAsset1 = ctx_.tx[sfAsset1];
    auto const saAsset2 = ctx_.tx[sfAsset2];

    auto const ammHash = calcAMMGroupHash(saAsset1.issue(), saAsset2.issue());

    // Check if AMM already exists for the token pair
    if (sb.peek(keylet::amm(ammHash)))
    {
        JLOG(j_.debug()) << "AMM Instance: ltAMM already exists.";
        return {tecAMM_EXISTS, false};
    }

    auto const ammAccountID = calcAccountID(sb.info().parentHash, ammHash);

    // AMM account already exists (should not happen)
    if (sb.peek(keylet::account(ammAccountID)))
    {
        JLOG(j_.debug()) << "AMM Instance: AMM already exists.";
        return {tecAMM_EXISTS, false};
    }

    // LP Token already exists. (should not happen)
    auto const lptIssue = calcLPTIssue(ammAccountID);
    if (sb.read(keylet::line(ammAccountID, lptIssue)))
    {
        JLOG(j_.debug()) << "AMM Instance: LP Token already exists.";
        return {tecAMM_EXISTS, false};
    }

    // Create AMM Root Account.
    auto sleAMMRoot = std::make_shared<SLE>(keylet::account(ammAccountID));
    sleAMMRoot->setAccountID(sfAccount, ammAccountID);
    sleAMMRoot->setFieldAmount(sfBalance, STAmount{});
    std::uint32_t const seqno{
        view().rules().enabled(featureDeletableAccounts) ? view().seq() : 1};
    sleAMMRoot->setFieldU32(sfSequence, seqno);
    // Ignore reserves requirement, disable the master key, and allow default
    // rippling (AMM LPToken can be used a token in another AMM, which must
    // support payments and offer crossing).
    sleAMMRoot->setFieldU32(
        sfFlags, lsfAMM | lsfDisableMaster | lsfDefaultRipple);
    sb.insert(sleAMMRoot);

    // Calculate initial LPT balance.
    auto const lpTokens = calcAMMLPT(saAsset1, saAsset2, lptIssue);

    // Create ltAMM
    auto ammSle = std::make_shared<SLE>(keylet::amm(ammHash));
    ammSle->setFieldU16(sfTradingFee, ctx_.tx[sfTradingFee]);
    ammSle->setAccountID(sfAMMAccount, ammAccountID);
    ammSle->setFieldAmount(sfLPTokenBalance, lpTokens);
    auto const& issue1 = saAsset1.issue() < saAsset2.issue() ? saAsset1.issue()
                                                             : saAsset2.issue();
    auto const& issue2 =
        issue1 == saAsset1.issue() ? saAsset2.issue() : saAsset1.issue();
    ammSle->makeFieldPresent(sfAMMToken);
    auto& ammToken = ammSle->peekFieldObject(sfAMMToken);
    auto setToken = [&](SField const& field, Issue const& issue) {
        ammToken.makeFieldPresent(field);
        auto& token = ammToken.peekFieldObject(field);
        token.setFieldH160(sfTokenCurrency, issue.currency);
        token.setFieldH160(sfTokenIssuer, issue.account);
    };
    setToken(sfToken1, issue1);
    setToken(sfToken2, issue2);
    sb.insert(ammSle);

    // Send LPT to LP.
    auto res = accountSend(sb, ammAccountID, account_, lpTokens, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(j_.debug()) << "AMM Instance: failed to send LPT " << lpTokens;
        return {res, false};
    }

    // Send asset1.
    res = ammSend(sb, account_, ammAccountID, saAsset1, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(j_.debug()) << "AMM Instance: failed to send " << saAsset1;
        return {res, false};
    }

    // Send asset2.
    res = ammSend(sb, account_, ammAccountID, saAsset2, ctx_.journal);
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