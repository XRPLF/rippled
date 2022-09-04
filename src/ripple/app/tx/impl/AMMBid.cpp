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
#include <ripple/app/tx/impl/AMMBid.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

TxConsequences
AMMBid::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{ctx.tx};
}

NotTEC
AMMBid::preflight(PreflightContext const& ctx)
{
    if (!ammRequiredAmendments(ctx.rules))
        return temDISABLED;

    auto const ret = preflight1(ctx);
    if (!isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.debug()) << "AMM Bid: invalid flags.";
        return temINVALID_FLAG;
    }

    if (ctx.tx[~sfMinSlotPrice] && ctx.tx[~sfMaxSlotPrice])
    {
        JLOG(ctx.j.debug()) << "AMM Bid: invalid options.";
        return temBAD_AMM_OPTIONS;
    }

    if (invalidAmount(ctx.tx[~sfMinSlotPrice]) ||
        invalidAmount(ctx.tx[~sfMaxSlotPrice]))
    {
        JLOG(ctx.j.debug()) << "AMM Bid: invalid min slot price.";
        return temBAD_AMM_TOKENS;
    }

    return preflight2(ctx);
}

TER
AMMBid::preclaim(PreclaimContext const& ctx)
{
    if (!ctx.view.read(keylet::account(ctx.tx[sfAccount])))
    {
        JLOG(ctx.j.debug()) << "AMM Bid: Invalid account.";
        return terNO_ACCOUNT;
    }

    auto const ammSle = getAMMSle(ctx.view, ctx.tx[sfAMMID]);
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Bid: Invalid AMM account.";
        return terNO_ACCOUNT;
    }

    if (ctx.tx.isFieldPresent(sfAuthAccounts))
    {
        auto const authAccounts = ctx.tx.getFieldArray(sfAuthAccounts);
        if (authAccounts.size() > 4)
        {
            JLOG(ctx.j.debug()) << "AMM Bid: Invalid number of AuthAccounts.";
            return temBAD_AMM_OPTIONS;
        }

        for (auto& account : authAccounts)
        {
            if (!ctx.view.read(
                    keylet::account(account.getAccountID(sfAccount))))
            {
                JLOG(ctx.j.debug()) << "AMM Bid: Invalid Account.";
                return terNO_ACCOUNT;
            }
        }
    }

    auto const lpTokens = lpHolds(
        ctx.view, ammSle->getAccountID(sfAMMAccount), ctx.tx[sfAccount], ctx.j);

    if (auto const minSlotPrice = ctx.tx[~sfMinSlotPrice])
    {
        if (*minSlotPrice > lpTokens)
        {
            JLOG(ctx.j.debug()) << "AMM Bid: Invalid Tokens.";
            return tecAMM_INVALID_TOKENS;
        }
        if (minSlotPrice->issue() != lpTokens.issue())
        {
            JLOG(ctx.j.debug()) << "AMM Bid: Invalid LPToken.";
            return temBAD_AMM_TOKENS;
        }
    }

    if (auto const maxSlotPrice = ctx.tx[~sfMaxSlotPrice])
    {
        if (*maxSlotPrice > lpTokens)
        {
            JLOG(ctx.j.debug()) << "AMM Bid: Invalid Tokens.";
            return tecAMM_INVALID_TOKENS;
        }
        if (maxSlotPrice->issue() != lpTokens.issue())
        {
            JLOG(ctx.j.debug()) << "AMM Bid: Invalid LPToken.";
            return temBAD_AMM_TOKENS;
        }
    }

    return tesSUCCESS;
}

void
AMMBid::preCompute()
{
    return Transactor::preCompute();
}

std::pair<TER, bool>
AMMBid::applyGuts(Sandbox& sb)
{
    using namespace std::chrono;
    auto const amm = getAMMSle(sb, ctx_.tx[sfAMMID]);
    assert(amm);
    auto const ammAccount = amm->getAccountID(sfAMMAccount);
    auto const lptAMMBalance = amm->getFieldAmount(sfLPTokenBalance);
    auto const lpTokens = lpHolds(sb, ammAccount, account_, ctx_.journal);
    if (!amm->isFieldPresent(sfAuctionSlot))
        amm->makeFieldPresent(sfAuctionSlot);
    auto& auctionSlot = amm->peekFieldObject(sfAuctionSlot);
    auto const current =
        duration_cast<seconds>(
            ctx_.view().info().parentCloseTime.time_since_epoch())
            .count();

    std::uint32_t constexpr totalSlotTimeSecs = 24 * 3600;
    std::uint32_t constexpr nIntervals = 20;
    std::uint32_t constexpr intervalDuration = totalSlotTimeSecs / nIntervals;

    // If seated then it is the current slot-holder time slot, otherwise
    // the auction slot is not owned. Slot range is in {0-19}
    auto const timeSlot = [&]() -> std::optional<std::uint8_t> {
        if (auctionSlot.isFieldPresent(sfTimeStamp))
        {
            auto const stamp = auctionSlot.getFieldU32(sfTimeStamp);
            auto const diff = current - stamp;
            if (diff < totalSlotTimeSecs)
                return (std::int64_t)(diff / intervalDuration);
        }
        return std::nullopt;
    }();

    // Account must exist, is LP, and the slot not expired.
    auto validOwner = [&](AccountID const& account) {
        return sb.read(keylet::account(account)) &&
            lpHolds(sb, ammAccount, account, ctx_.journal) != beast::zero &&
            timeSlot && *timeSlot < 19;
    };

    auto updateSlot = [&](std::uint32_t fee,
                          Number const& minPrice,
                          Number const& burn) -> TER {
        auctionSlot.setAccountID(sfAccount, account_);
        auctionSlot.setFieldU32(sfTimeStamp, current);
        auctionSlot.setFieldU32(sfDiscountedFee, fee);
        auctionSlot.setFieldAmount(
            sfPrice, toSTAmount(lpTokens.issue(), minPrice));
        if (ctx_.tx.isFieldPresent(sfAuthAccounts))
            auctionSlot.setFieldArray(
                sfAuthAccounts, ctx_.tx.getFieldArray(sfAuthAccounts));
        // Burn the remaining bid amount
        auto const saBurn = toSTAmount(lpTokens.issue(), burn);
        auto res =
            redeemIOU(sb, account_, saBurn, lpTokens.issue(), ctx_.journal);
        if (res != tesSUCCESS)
        {
            JLOG(ctx_.journal.debug()) << "AMM Bid: failed to redeem.";
            return res;
        }
        amm->setFieldAmount(sfLPTokenBalance, lptAMMBalance - saBurn);
        sb.update(amm);
        return tesSUCCESS;
    };

    TER res = tesSUCCESS;

    auto const minSlotPrice = ctx_.tx[~sfMinSlotPrice];
    auto const maxSlotPrice = ctx_.tx[~sfMaxSlotPrice];

    Number const MinSlotPrice = lptAMMBalance / 100000;  // 0.001% TBD
    // Arbitrager's bid price
    auto const bidPrice = [&]() -> Number {
        if (minSlotPrice)
            return *minSlotPrice;
        else if (maxSlotPrice)
            return *maxSlotPrice;
        else
            return 0;
    }();

    // No one owns the slot or expired slot.
    // The bidder pays MinSlotPrice
    if (!auctionSlot.isFieldPresent(sfAccount) ||
        !validOwner(auctionSlot.getAccountID(sfAccount)))
    {
        res = updateSlot(0, MinSlotPrice, MinSlotPrice);
    }
    else
    {
        // Price the slot was purchased at.
        Number const pricePurchased = auctionSlot.getFieldAmount(sfPrice);
        auto const fractionUsed = (Number(*timeSlot) + 1) / nIntervals;
        auto const fractionRemaining = Number(1) - fractionUsed;
        auto computedPrice = [&]() -> Number {
            Number const p1_05 = Number(105) / 100;
            // First interval slot price
            if (*timeSlot == 0)
                return pricePurchased * p1_05;
            // Other intervals slot price
            else
                return pricePurchased * p1_05 * (1 - power(fractionUsed, 60)) +
                    MinSlotPrice;
        }();

        // If max pricePurchased then don't pay more than the max
        // pricePurchased.
        if (maxSlotPrice && computedPrice > *maxSlotPrice)
        {
            JLOG(ctx_.journal.debug()) << "AMM Bid: computed pricePurchased "
                                          "exceeds max pricePurchased.";
            return {tecAMM_FAILED_BID, false};
        }

        auto const payPrice = [&]() -> Number {
            // Bidder pays max(bidPrice, computedPrice)
            if (minSlotPrice)
                return bidPrice > computedPrice ? bidPrice : computedPrice;
            else if (maxSlotPrice)
                return bidPrice;  // max slot price is less than computed price
            else
                return computedPrice;
        }();

        res = updateSlot(0, payPrice, payPrice * (1 - fractionRemaining));
        if (res != tesSUCCESS)
            return {res, false};
        // Refund the previous owner. If the time slot is 0 then
        // the owner is refunded full amount.
        res = accountSend(
            sb,
            account_,
            auctionSlot.getAccountID(sfAccount),
            toSTAmount(lpTokens.issue(), fractionRemaining * payPrice),
            ctx_.journal);
        if (res != tesSUCCESS)
        {
            JLOG(ctx_.journal.debug()) << "AMM Bid: failed to refund.";
            return {res, false};
        }
    }

    return {tesSUCCESS, true};
}

TER
AMMBid::doApply()
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
