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

#include <ripple/app/tx/impl/AMMBid.h>

#include <ripple/app/misc/AMM.h>
#include <ripple/app/misc/AMM_formulae.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STIssue.h>
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
    if (!ammEnabled(ctx.rules))
        return temDISABLED;

    auto const ret = preflight1(ctx);
    if (!isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.debug()) << "AMM Bid: invalid flags.";
        return temINVALID_FLAG;
    }

    if (auto const res = invalidAMMAssetPair(ctx.tx[sfAsset], ctx.tx[sfAsset2]))
    {
        JLOG(ctx.j.debug()) << "AMM Bid: Invalid asset pair.";
        return res;
    }

    if (auto const res = invalidAMMAmount(ctx.tx[~sfBidMin]))
    {
        JLOG(ctx.j.debug()) << "AMM Bid: invalid min slot price.";
        return res;
    }

    if (auto const res = invalidAMMAmount(ctx.tx[~sfBidMax]))
    {
        JLOG(ctx.j.debug()) << "AMM Bid: invalid max slot price.";
        return res;
    }

    if (ctx.tx.isFieldPresent(sfAuthAccounts))
    {
        std::uint8_t constexpr maxAuthAccounts = 4;
        if (auto const authAccounts = ctx.tx.getFieldArray(sfAuthAccounts);
            authAccounts.size() > maxAuthAccounts)
        {
            JLOG(ctx.j.debug()) << "AMM Bid: Invalid number of AuthAccounts.";
            return temBAD_AMM_OPTIONS;
        }
    }

    return preflight2(ctx);
}

TER
AMMBid::preclaim(PreclaimContext const& ctx)
{
    auto const ammSle = getAMMSle(ctx.view, ctx.tx[sfAsset], ctx.tx[sfAsset2]);
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Bid: Invalid asset pair.";
        return terNO_AMM;
    }

    if (ctx.tx.isFieldPresent(sfAuthAccounts))
    {
        for (auto const& account : ctx.tx.getFieldArray(sfAuthAccounts))
        {
            if (!ctx.view.read(keylet::account(account[sfAccount])))
            {
                JLOG(ctx.j.debug()) << "AMM Bid: Invalid Account.";
                return terNO_ACCOUNT;
            }
        }
    }

    auto const lpTokens =
        ammLPHolds(ctx.view, **ammSle, ctx.tx[sfAccount], ctx.j);
    auto const lpTokensBalance = (**ammSle)[sfLPTokenBalance];

    auto const bidMin = ctx.tx[~sfBidMin];

    if (bidMin)
    {
        if (bidMin->issue() != lpTokens.issue())
        {
            JLOG(ctx.j.debug()) << "AMM Bid: Invalid LPToken.";
            return temBAD_AMM_TOKENS;
        }
        if (*bidMin > lpTokens || *bidMin >= lpTokensBalance)
        {
            JLOG(ctx.j.debug()) << "AMM Bid: Invalid Tokens.";
            return tecAMM_INVALID_TOKENS;
        }
    }

    auto const bidMax = ctx.tx[~sfBidMax];
    if (bidMax)
    {
        if (bidMax->issue() != lpTokens.issue())
        {
            JLOG(ctx.j.debug()) << "AMM Bid: Invalid LPToken.";
            return temBAD_AMM_TOKENS;
        }
        if (*bidMax > lpTokens || *bidMax >= lpTokensBalance)
        {
            JLOG(ctx.j.debug()) << "AMM Bid: Invalid Tokens.";
            return tecAMM_INVALID_TOKENS;
        }
    }

    if (bidMin && bidMax && bidMin >= bidMax)
    {
        JLOG(ctx.j.debug()) << "AMM Bid: Invalid Max/MinSlotPrice.";
        return tecAMM_INVALID_TOKENS;
    }

    return tesSUCCESS;
}

static std::pair<TER, bool>
applyBid(
    ApplyContext& ctx_,
    Sandbox& sb,
    AccountID const& account_,
    beast::Journal j_)
{
    using namespace std::chrono;
    auto const amm = getAMMSle(sb, ctx_.tx[sfAsset], ctx_.tx[sfAsset2]);
    if (!amm)
        return {amm.error(), false};
    STAmount const lptAMMBalance = (**amm)[sfLPTokenBalance];
    auto const lpTokens = ammLPHolds(sb, **amm, account_, ctx_.journal);
    if (!(*amm)->isFieldPresent(sfAuctionSlot))
        (*amm)->makeFieldPresent(sfAuctionSlot);
    auto& auctionSlot = (*amm)->peekFieldObject(sfAuctionSlot);
    auto const current =
        duration_cast<seconds>(
            ctx_.view().info().parentCloseTime.time_since_epoch())
            .count();

    std::uint32_t constexpr totalSlotTimeSecs = 24 * 3600;
    std::uint32_t constexpr nIntervals = 20;
    std::uint32_t constexpr tailingSlot = 19;

    // If seated then it is the current slot-holder time slot, otherwise
    // the auction slot is not owned. Slot range is in {0-19}
    auto const timeSlot = ammAuctionTimeSlot(current, auctionSlot);

    // Account must exist, is LP, and the slot not expired.
    auto validOwner = [&](AccountID const& account) {
        return sb.read(keylet::account(account)) &&
            ammLPHolds(sb, **amm, account, ctx_.journal) != beast::zero &&
            // Valid range is 0-19 but the tailing slot pays MinSlotPrice
            // and doesn't refund so the check is < instead of <= to optimize.
            timeSlot && *timeSlot < tailingSlot;
    };

    auto updateSlot = [&](std::uint32_t fee,
                          Number const& minPrice,
                          Number const& burn) -> TER {
        auctionSlot.setAccountID(sfAccount, account_);
        auctionSlot.setFieldU32(sfExpiration, current + totalSlotTimeSecs);
        auctionSlot.setFieldU32(sfDiscountedFee, fee);
        auctionSlot.setFieldAmount(
            sfPrice, toSTAmount(lpTokens.issue(), minPrice));
        if (ctx_.tx.isFieldPresent(sfAuthAccounts))
            auctionSlot.setFieldArray(
                sfAuthAccounts, ctx_.tx.getFieldArray(sfAuthAccounts));
        // Burn the remaining bid amount
        auto const saBurn = toSTAmount(lpTokens.issue(), burn);
        if (saBurn >= lptAMMBalance)
        {
            JLOG(ctx_.journal.debug())
                << "AMM Bid: invalid burn " << burn << " " << lptAMMBalance;
            return tecAMM_FAILED_BID;
        }
        auto res =
            redeemIOU(sb, account_, saBurn, lpTokens.issue(), ctx_.journal);
        if (res != tesSUCCESS)
        {
            JLOG(ctx_.journal.debug()) << "AMM Bid: failed to redeem.";
            return res;
        }
        (*amm)->setFieldAmount(sfLPTokenBalance, lptAMMBalance - saBurn);
        sb.update(*amm);
        return tesSUCCESS;
    };

    TER res = tesSUCCESS;

    auto const bidMin = ctx_.tx[~sfBidMin];
    auto const bidMax = ctx_.tx[~sfBidMax];

    Number const MinSlotPrice = 0;

    auto getPayPrice =
        [&](Number const& computedPrice) -> std::optional<Number> {
        // Both min/max bid price are defined
        if (bidMin && bidMax)
        {
            if (computedPrice >= *bidMin && computedPrice <= *bidMax)
                return computedPrice;
            JLOG(ctx_.journal.debug())
                << "AMM Bid: not in range " << computedPrice << *bidMin << " "
                << *bidMax;
            return std::nullopt;
        }
        // Bidder pays max(bidPrice, computedPrice)
        if (bidMin)
        {
            return std::max(computedPrice, Number(*bidMin));
        }
        else if (bidMax)
        {
            if (computedPrice <= *bidMax)
                return computedPrice;
            JLOG(ctx_.journal.debug())
                << "AMM Bid: not in range " << computedPrice << *bidMax;
            return std::nullopt;
        }
        else
            return computedPrice;
    };

    // No one owns the slot or expired slot.
    if (auto const acct = auctionSlot[~sfAccount]; !acct || !validOwner(*acct))
    {
        if (auto const payPrice = getPayPrice(MinSlotPrice); !payPrice)
            return {tecAMM_FAILED_BID, false};
        else
            res = updateSlot(0, *payPrice, *payPrice);
    }
    else
    {
        // Price the slot was purchased at.
        STAmount const pricePurchased = auctionSlot[sfPrice];
        auto const fractionUsed = (Number(*timeSlot) + 1) / nIntervals;
        auto const fractionRemaining = Number(1) - fractionUsed;
        auto const computedPrice = [&]() -> Number {
            auto const p1_05 = Number(105, -2);
            // First interval slot price
            if (*timeSlot == 0)
                return pricePurchased * p1_05 + MinSlotPrice;
            // Other intervals slot price
            return pricePurchased * p1_05 * (1 - power(fractionUsed, 60)) +
                MinSlotPrice;
        }();

        auto const payPrice = getPayPrice(computedPrice);

        if (!payPrice)
            return {tecAMM_FAILED_BID, false};

        res = updateSlot(0, *payPrice, *payPrice * (1 - fractionRemaining));
        if (res != tesSUCCESS)
            return {res, false};
        // Refund the previous owner. If the time slot is 0 then
        // the owner is refunded full amount.
        res = accountSend(
            sb,
            account_,
            auctionSlot[sfAccount],
            toSTAmount(lpTokens.issue(), fractionRemaining * *payPrice),
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

    auto const result = applyBid(ctx_, sb, account_, j_);
    if (result.second)
        sb.apply(ctx_.rawView());
    else
        sbCancel.apply(ctx_.rawView());

    return result.first;
}

}  // namespace ripple
