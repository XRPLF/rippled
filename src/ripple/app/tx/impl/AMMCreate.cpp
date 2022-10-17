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

#include <ripple/app/tx/impl/AMMCreate.h>

#include <ripple/app/ledger/OrderBookDB.h>
#include <ripple/app/misc/AMM.h>
#include <ripple/app/misc/AMM_formulae.h>
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
    if (!ammEnabled(ctx.rules))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.debug()) << "AMM Instance: invalid flags.";
        return temINVALID_FLAG;
    }

    auto const saAsset1 = ctx.tx[sfAsset1];
    auto const saAsset2 = ctx.tx[sfAsset2];

    if (saAsset1.issue() == saAsset2.issue())
    {
        JLOG(ctx.j.debug())
            << "AMM Instance: tokens can not have the same currency/issuer.";
        return temBAD_AMM_TOKENS;
    }

    if (auto const err = invalidAMMAmount(saAsset1))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: invalid asset1 amount.";
        return err;
    }

    if (auto const err = invalidAMMAmount(saAsset2))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: invalid asset2 amount.";
        return err;
    }

    if (ctx.tx[sfTradingFee] > 65000)
    {
        JLOG(ctx.j.debug()) << "AMM Instance: invalid trading fee.";
        return temBAD_FEE;
    }

    return preflight2(ctx);
}

TER
AMMCreate::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];
    auto const saAsset1 = ctx.tx[sfAsset1];
    auto const saAsset2 = ctx.tx[sfAsset2];

    if (auto const ter = requireAuth(ctx.view, saAsset1.issue(), accountID);
        ter != tesSUCCESS)
    {
        JLOG(ctx.j.debug())
            << "AMM Instance: account is not authorized, " << saAsset1.issue();
        return ter;
    }

    if (auto const ter = requireAuth(ctx.view, saAsset2.issue(), accountID);
        ter != tesSUCCESS)
    {
        JLOG(ctx.j.debug())
            << "AMM Instance: account is not authorized, " << saAsset2.issue();
        return ter;
    }

    if (isFrozen(ctx.view, saAsset1) || isFrozen(ctx.view, saAsset2))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: involves frozen asset.";
        return tecFROZEN;
    }

    // Check the reserve for LPToken trustline
    STAmount const xrpBalance = xrpLiquid(ctx.view, accountID, 1, ctx.j);
    // Insufficient reserve
    if (xrpBalance <= beast::zero)
    {
        JLOG(ctx.j.debug()) << "AMM Instance: insufficient reserves";
        return tecINSUF_RESERVE_LINE;
    }

    auto insufficientBalance = [&](STAmount const& asset) {
        if (isXRP(asset))
            return xrpBalance < asset;
        return accountID != asset.issue().account &&
            accountHolds(
                ctx.view,
                accountID,
                asset.issue().currency,
                asset.issue().account,
                FreezeHandling::fhZERO_IF_FROZEN,
                ctx.j) < asset;
    };

    if (insufficientBalance(saAsset1) || insufficientBalance(saAsset2))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: insufficient funds";
        return tecUNFUNDED_AMM;
    }

    return tesSUCCESS;
}

static std::pair<TER, bool>
applyCreate(
    ApplyContext& ctx_,
    Sandbox& sb,
    AccountID const& account_,
    beast::Journal j_)
{
    auto const saAsset1 = ctx_.tx[sfAsset1];
    auto const saAsset2 = ctx_.tx[sfAsset2];

    auto const ammKeylet = keylet::amm(saAsset1.issue(), saAsset2.issue());

    // Check if AMM already exists for the token pair
    if (sb.read(ammKeylet))
    {
        JLOG(j_.debug()) << "AMM Instance: ltAMM already exists.";
        return {tecAMM_EXISTS, false};
    }

    auto const ammAccount = ammAccountID(sb.info().parentHash, ammKeylet.key);

    // AMM account already exists (should not happen)
    if (sb.read(keylet::account(ammAccount)))
    {
        JLOG(j_.debug()) << "AMM Instance: AMM already exists.";
        return {tecAMM_EXISTS, false};
    }

    // LP Token already exists. (should not happen)
    auto const lptIss = lptIssue(ammAccount);
    if (sb.read(keylet::line(ammAccount, lptIss)))
    {
        JLOG(j_.debug()) << "AMM Instance: LP Token already exists.";
        return {tecAMM_EXISTS, false};
    }

    // Create AMM Root Account.
    auto sleAMMRoot = std::make_shared<SLE>(keylet::account(ammAccount));
    sleAMMRoot->setAccountID(sfAccount, ammAccount);
    sleAMMRoot->setFieldAmount(sfBalance, STAmount{});
    std::uint32_t const seqno{
        ctx_.view().rules().enabled(featureDeletableAccounts)
            ? ctx_.view().seq()
            : 1};
    sleAMMRoot->setFieldU32(sfSequence, seqno);
    // Ignore reserves requirement, disable the master key, allow default
    // rippling (AMM LPToken can be used as a token in another AMM, which must
    // support payments and offer crossing), and enable deposit autherization to
    // prevent payments into AMM.
    sleAMMRoot->setFieldU32(
        sfFlags, lsfAMM | lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);
    sb.insert(sleAMMRoot);

    // Calculate initial LPT balance.
    auto const lpTokens = ammLPTokens(saAsset1, saAsset2, lptIss);

    // Create ltAMM
    auto ammSle = std::make_shared<SLE>(ammKeylet);
    ammSle->setFieldU16(sfTradingFee, ctx_.tx[sfTradingFee]);
    ammSle->setAccountID(sfAMMAccount, ammAccount);
    ammSle->setFieldAmount(sfLPTokenBalance, lpTokens);
    auto const& [issue1, issue2] =
        std::minmax(saAsset1.issue(), saAsset2.issue());
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
    auto res = accountSend(sb, ammAccount, account_, lpTokens, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(j_.debug()) << "AMM Instance: failed to send LPT " << lpTokens;
        return {res, false};
    }

    // Send asset1.
    res = ammSend(sb, account_, ammAccount, saAsset1, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(j_.debug()) << "AMM Instance: failed to send " << saAsset1;
        return {res, false};
    }

    // Send asset2.
    res = ammSend(sb, account_, ammAccount, saAsset2, ctx_.journal);
    if (res != tesSUCCESS)
        JLOG(j_.debug()) << "AMM Instance: failed to send " << saAsset2;
    else
    {
        JLOG(j_.debug()) << "AMM Instance: success " << ammAccount << " "
                         << ammKeylet.key << " " << lptIss << " " << saAsset1
                         << " " << saAsset2;
        auto addOrderBook = [&](Issue const& issueIn,
                                Issue const& issueOut,
                                std::uint64_t uRate) {
            Book const book{issueIn, issueOut};
            auto const dir = keylet::quality(keylet::book(book), uRate);
            if (auto const bookExisted = static_cast<bool>(sb.peek(dir));
                !bookExisted)
                ctx_.app.getOrderBookDB().addOrderBook(book);
        };
        addOrderBook(
            saAsset1.issue(), saAsset2.issue(), getRate(saAsset2, saAsset1));
        addOrderBook(
            saAsset2.issue(), saAsset1.issue(), getRate(saAsset1, saAsset2));
    }

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

    auto const result = applyCreate(ctx_, sb, account_, j_);
    if (result.second)
        sb.apply(ctx_.rawView());
    else
        sbCancel.apply(ctx_.rawView());

    return result.first;
}

}  // namespace ripple