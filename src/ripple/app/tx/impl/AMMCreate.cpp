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
#include <ripple/app/misc/AMMHelpers.h>
#include <ripple/app/misc/AMMUtils.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/AMMCore.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STIssue.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

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

    auto const amount = ctx.tx[sfAmount];
    auto const amount2 = ctx.tx[sfAmount2];

    if (amount.issue() == amount2.issue())
    {
        JLOG(ctx.j.debug())
            << "AMM Instance: tokens can not have the same currency/issuer.";
        return temAMM_BAD_TOKENS;
    }

    if (auto const err = invalidAMMAmount(amount))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: invalid asset1 amount.";
        return err;
    }

    if (auto const err = invalidAMMAmount(amount2))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: invalid asset2 amount.";
        return err;
    }

    if (ctx.tx[sfTradingFee] > TradingFeeThreshold)
    {
        JLOG(ctx.j.debug()) << "AMM Instance: invalid trading fee.";
        return temBAD_FEE;
    }

    return preflight2(ctx);
}

FeeUnit64
AMMCreate::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // The fee required for AccountDelete is one owner reserve.  But the
    // owner reserve is stored in drops.  We need to convert it to fee units.
    Fees const& fees{view.fees()};
    std::pair<bool, FeeUnit64> const mulDivResult{
        mulDiv(fees.increment, safe_cast<FeeUnit64>(fees.units), fees.base)};
    if (mulDivResult.first)
        return mulDivResult.second;

    // If mulDiv returns false then overflow happened.  Punt by using the
    // standard calculation.
    return Transactor::calculateBaseFee(view, tx);
}

TER
AMMCreate::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];
    auto const amount = ctx.tx[sfAmount];
    auto const amount2 = ctx.tx[sfAmount2];

    // Check if AMM already exists for the token pair
    if (auto const ammKeylet = keylet::amm(amount.issue(), amount2.issue());
        ctx.view.read(ammKeylet))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: ltAMM already exists.";
        return tecDUPLICATE;
    }

    if (auto const ter = requireAuth(ctx.view, amount.issue(), accountID);
        ter != tesSUCCESS)
    {
        JLOG(ctx.j.debug())
            << "AMM Instance: account is not authorized, " << amount.issue();
        return ter;
    }

    if (auto const ter = requireAuth(ctx.view, amount2.issue(), accountID);
        ter != tesSUCCESS)
    {
        JLOG(ctx.j.debug())
            << "AMM Instance: account is not authorized, " << amount2.issue();
        return ter;
    }

    if (isGlobalFrozen(ctx.view, amount.getIssuer()) ||
        isGlobalFrozen(ctx.view, amount2.getIssuer()))
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

    if (insufficientBalance(amount) || insufficientBalance(amount2))
    {
        JLOG(ctx.j.debug())
            << "AMM Instance: insufficient funds, " << amount << " " << amount2;
        return tecAMM_UNFUNDED;
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
    auto const amount = ctx_.tx[sfAmount];
    auto const amount2 = ctx_.tx[sfAmount2];

    auto const ammKeylet = keylet::amm(amount.issue(), amount2.issue());

    // Mitigate same account exists possibility
    auto const ammAccount = [&]() -> Expected<AccountID, TER> {
        std::uint16_t constexpr maxAccountAttempts = 256;
        for (auto p = 0; p < maxAccountAttempts; ++p)
        {
            auto const ammAccount =
                ammAccountID(p, sb.info().parentHash, ammKeylet.key);
            if (!sb.read(keylet::account(ammAccount)))
                return ammAccount;
        }
        return Unexpected(tecDUPLICATE);
    }();

    // AMM account already exists (should not happen)
    if (!ammAccount)
    {
        JLOG(j_.error()) << "AMM Instance: AMM already exists.";
        return {ammAccount.error(), false};
    }

    // LP Token already exists. (should not happen)
    auto const lptIss = ammLPTIssue(
        amount.issue().currency, amount2.issue().currency, *ammAccount);
    if (sb.read(keylet::line(*ammAccount, lptIss)))
    {
        JLOG(j_.error()) << "AMM Instance: LP Token already exists.";
        return {tecDUPLICATE, false};
    }

    // Create AMM Root Account.
    auto sleAMMRoot = std::make_shared<SLE>(keylet::account(*ammAccount));
    sleAMMRoot->setAccountID(sfAccount, *ammAccount);
    sleAMMRoot->setFieldAmount(sfBalance, STAmount{});
    std::uint32_t const seqno{
        ctx_.view().rules().enabled(featureDeletableAccounts)
            ? ctx_.view().seq()
            : 1};
    sleAMMRoot->setFieldU32(sfSequence, seqno);
    // Ignore reserves requirement, disable the master key, allow default
    // rippling (AMM LPToken can be used as a token in another AMM, which must
    // support payments and offer crossing), and enable deposit authorization to
    // prevent payments into AMM.
    // Note, that the trustlines created by AMM have 0 credit limit. This
    // prevents shifting the balance between accounts via AMM. This is
    // a desired behavior, though not an ideal solution; i.e. 0 means no
    // trust except for AMMs where it means infinite trust.
    sleAMMRoot->setFieldU32(
        sfFlags, lsfAMM | lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);
    sb.insert(sleAMMRoot);

    // Calculate initial LPT balance.
    auto const lpTokens = ammLPTokens(amount, amount2, lptIss);

    // Create ltAMM
    auto ammSle = std::make_shared<SLE>(ammKeylet);
    ammSle->setFieldU16(sfTradingFee, ctx_.tx[sfTradingFee]);
    ammSle->setAccountID(sfAMMAccount, *ammAccount);
    ammSle->setFieldAmount(sfLPTokenBalance, lpTokens);
    auto const& [issue1, issue2] = std::minmax(amount.issue(), amount2.issue());
    ammSle->setFieldIssue(sfAsset, STIssue{sfAsset, issue1});
    ammSle->setFieldIssue(sfAsset2, STIssue{sfAsset2, issue2});
    // AMM creator gets the voting slot.
    STArray voteSlots;
    STObject voteEntry{sfVoteEntry};
    voteEntry.setFieldU16(sfTradingFee, ctx_.tx[sfTradingFee]);
    voteEntry.setFieldU32(sfVoteWeight, 100000);
    voteEntry.setAccountID(sfAccount, account_);
    voteSlots.push_back(voteEntry);
    ammSle->setFieldArray(sfVoteSlots, voteSlots);
    // AMM creator gets the auction slot for free.
    auto& auctionSlot = ammSle->peekFieldObject(sfAuctionSlot);
    auctionSlot.setAccountID(sfAccount, account_);
    // current + sec in 24h
    auto const expiration =
        std::chrono::duration_cast<std::chrono::seconds>(
            ctx_.view().info().parentCloseTime.time_since_epoch())
            .count() +
        24 * 3600;
    auctionSlot.setFieldU32(sfExpiration, expiration);
    auctionSlot.setFieldU32(sfDiscountedFee, 0);
    auctionSlot.setFieldAmount(sfPrice, STAmount{lpTokens.issue(), 0});
    sb.insert(ammSle);

    // Send LPT to LP.
    auto res = accountSend(sb, *ammAccount, account_, lpTokens, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(j_.debug()) << "AMM Instance: failed to send LPT " << lpTokens;
        return {res, false};
    }

    auto sendAndTrustSet = [&](STAmount const& amount) -> TER {
        if (auto const res =
                accountSend(sb, account_, *ammAccount, amount, ctx_.journal))
            return res;
        // Set AMM flag on AMM trustline
        if (!isXRP(amount))
        {
            if (SLE::pointer sleRippleState =
                    sb.peek(keylet::line(*ammAccount, amount.issue()));
                !sleRippleState)
                return tecINTERNAL;
            else
            {
                auto const flags = sleRippleState->getFlags();
                sleRippleState->setFieldU32(sfFlags, flags | lsfAMMNode);
                sb.update(sleRippleState);
            }
        }
        return tesSUCCESS;
    };

    // Send asset1.
    res = sendAndTrustSet(amount);
    if (res != tesSUCCESS)
    {
        JLOG(j_.debug()) << "AMM Instance: failed to send " << amount;
        return {res, false};
    }

    // Send asset2.
    res = sendAndTrustSet(amount2);
    if (res != tesSUCCESS)
    {
        JLOG(j_.debug()) << "AMM Instance: failed to send " << amount2;
        return {res, false};
    }

    JLOG(j_.debug()) << "AMM Instance: success " << *ammAccount << " "
                     << ammKeylet.key << " " << lptIss << " " << amount << " "
                     << amount2;
    auto addOrderBook =
        [&](Issue const& issueIn, Issue const& issueOut, std::uint64_t uRate) {
            Book const book{issueIn, issueOut};
            auto const dir = keylet::quality(keylet::book(book), uRate);
            if (auto const bookExisted = static_cast<bool>(sb.read(dir));
                !bookExisted)
                ctx_.app.getOrderBookDB().addOrderBook(book);
        };
    addOrderBook(amount.issue(), amount2.issue(), getRate(amount2, amount));
    addOrderBook(amount2.issue(), amount.issue(), getRate(amount, amount2));

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
