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

#include <xrpld/app/tx/detail/AMMBid.h>

#include <xrpld/app/misc/AMMHelpers.h>
#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/View.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
AMMBid::preflight(PreflightContext const& ctx)
{
    if (!ammEnabled(ctx.rules))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
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

    if (auto const bidMin = ctx.tx[~sfBidMin])
    {
        if (auto const res = invalidAMMAmount(*bidMin))
        {
            JLOG(ctx.j.debug()) << "AMM Bid: invalid min slot price.";
            return res;
        }
    }

    if (auto const bidMax = ctx.tx[~sfBidMax])
    {
        if (auto const res = invalidAMMAmount(*bidMax))
        {
            JLOG(ctx.j.debug()) << "AMM Bid: invalid max slot price.";
            return res;
        }
    }

    if (ctx.tx.isFieldPresent(sfAuthAccounts))
    {
        if (auto const authAccounts = ctx.tx.getFieldArray(sfAuthAccounts);
            authAccounts.size() > AUCTION_SLOT_MAX_AUTH_ACCOUNTS)
        {
            JLOG(ctx.j.debug()) << "AMM Bid: Invalid number of AuthAccounts.";
            return temMALFORMED;
        }
    }

    return preflight2(ctx);
}

TER
AMMBid::preclaim(PreclaimContext const& ctx)
{
    auto const ammSle =
        ctx.view.read(keylet::amm(ctx.tx[sfAsset], ctx.tx[sfAsset2]));
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Bid: Invalid asset pair.";
        return terNO_AMM;
    }

    auto const lpTokensBalance = (*ammSle)[sfLPTokenBalance];
    if (lpTokensBalance == beast::zero)
        return tecAMM_EMPTY;

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
        ammLPHolds(ctx.view, *ammSle, ctx.tx[sfAccount], ctx.j);
    // Not LP
    if (lpTokens == beast::zero)
    {
        JLOG(ctx.j.debug()) << "AMM Bid: account is not LP.";
        return tecAMM_INVALID_TOKENS;
    }

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

    if (bidMin && bidMax && bidMin > bidMax)
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
    auto const ammSle =
        sb.peek(keylet::amm(ctx_.tx[sfAsset], ctx_.tx[sfAsset2]));
    if (!ammSle)
        return {tecINTERNAL, false};
    STAmount const lptAMMBalance = (*ammSle)[sfLPTokenBalance];
    auto const lpTokens = ammLPHolds(sb, *ammSle, account_, ctx_.journal);
    auto const& rules = ctx_.view().rules();
    if (!rules.enabled(fixInnerObjTemplate))
    {
        if (!ammSle->isFieldPresent(sfAuctionSlot))
            ammSle->makeFieldPresent(sfAuctionSlot);
    }
    else
    {
        ASSERT(
            ammSle->isFieldPresent(sfAuctionSlot),
            "ripple::applyBid : has auction slot");
        if (!ammSle->isFieldPresent(sfAuctionSlot))
            return {tecINTERNAL, false};
    }
    auto& auctionSlot = ammSle->peekFieldObject(sfAuctionSlot);
    auto const current =
        duration_cast<seconds>(
            ctx_.view().info().parentCloseTime.time_since_epoch())
            .count();
    // Auction slot discounted fee
    auto const discountedFee =
        (*ammSle)[sfTradingFee] / AUCTION_SLOT_DISCOUNTED_FEE_FRACTION;
    auto const tradingFee = getFee((*ammSle)[sfTradingFee]);
    // Min price
    auto const minSlotPrice =
        lptAMMBalance * tradingFee / AUCTION_SLOT_MIN_FEE_FRACTION;

    std::uint32_t constexpr tailingSlot = AUCTION_SLOT_TIME_INTERVALS - 1;

    // If seated then it is the current slot-holder time slot, otherwise
    // the auction slot is not owned. Slot range is in {0-19}
    auto const timeSlot = ammAuctionTimeSlot(current, auctionSlot);

    // Account must exist and the slot not expired.
    auto validOwner = [&](AccountID const& account) {
        // Valid range is 0-19 but the tailing slot pays MinSlotPrice
        // and doesn't refund so the check is < instead of <= to optimize.
        return timeSlot && *timeSlot < tailingSlot &&
            sb.read(keylet::account(account));
    };

    auto updateSlot = [&](std::uint32_t fee,
                          Number const& minPrice,
                          Number const& burn) -> TER {
        auctionSlot.setAccountID(sfAccount, account_);
        auctionSlot.setFieldU32(sfExpiration, current + TOTAL_TIME_SLOT_SECS);
        if (fee != 0)
            auctionSlot.setFieldU16(sfDiscountedFee, fee);
        else if (auctionSlot.isFieldPresent(sfDiscountedFee))
            auctionSlot.makeFieldAbsent(sfDiscountedFee);
        auctionSlot.setFieldAmount(
            sfPrice, toSTAmount(lpTokens.issue(), minPrice));
        if (ctx_.tx.isFieldPresent(sfAuthAccounts))
            auctionSlot.setFieldArray(
                sfAuthAccounts, ctx_.tx.getFieldArray(sfAuthAccounts));
        else
            auctionSlot.makeFieldAbsent(sfAuthAccounts);
        // Burn the remaining bid amount
        auto const saBurn = adjustLPTokens(
            lptAMMBalance, toSTAmount(lptAMMBalance.issue(), burn), false);
        if (saBurn >= lptAMMBalance)
        {
            // This error case should never occur.
            JLOG(ctx_.journal.fatal())
                << "AMM Bid: LP Token burn exceeds AMM balance " << burn << " "
                << lptAMMBalance;
            return tecINTERNAL;
        }
        auto res =
            redeemIOU(sb, account_, saBurn, lpTokens.issue(), ctx_.journal);
        if (res != tesSUCCESS)
        {
            JLOG(ctx_.journal.debug()) << "AMM Bid: failed to redeem.";
            return res;
        }
        ammSle->setFieldAmount(sfLPTokenBalance, lptAMMBalance - saBurn);
        sb.update(ammSle);
        return tesSUCCESS;
    };

    TER res = tesSUCCESS;

    auto const bidMin = ctx_.tx[~sfBidMin];
    auto const bidMax = ctx_.tx[~sfBidMax];

    auto getPayPrice =
        [&](Number const& computedPrice) -> Expected<Number, TER> {
        auto const payPrice = [&]() -> std::optional<Number> {
            // Both min/max bid price are defined
            if (bidMin && bidMax)
            {
                if (computedPrice <= *bidMax)
                    return std::max(computedPrice, Number(*bidMin));
                JLOG(ctx_.journal.debug())
                    << "AMM Bid: not in range " << computedPrice << " "
                    << *bidMin << " " << *bidMax;
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
                JLOG(ctx_.journal.debug()) << "AMM Bid: not in range "
                                           << computedPrice << " " << *bidMax;
                return std::nullopt;
            }
            else
                return computedPrice;
        }();
        if (!payPrice)
            return Unexpected(tecAMM_FAILED);
        else if (payPrice > lpTokens)
            return Unexpected(tecAMM_INVALID_TOKENS);
        return *payPrice;
    };

    // No one owns the slot or expired slot.
    if (auto const acct = auctionSlot[~sfAccount]; !acct || !validOwner(*acct))
    {
        if (auto const payPrice = getPayPrice(minSlotPrice); !payPrice)
            return {payPrice.error(), false};
        else
            res = updateSlot(discountedFee, *payPrice, *payPrice);
    }
    else
    {
        // Price the slot was purchased at.
        STAmount const pricePurchased = auctionSlot[sfPrice];
        ASSERT(timeSlot.has_value(), "ripple::applyBid : timeSlot is set");
        auto const fractionUsed =
            (Number(*timeSlot) + 1) / AUCTION_SLOT_TIME_INTERVALS;
        auto const fractionRemaining = Number(1) - fractionUsed;
        auto const computedPrice = [&]() -> Number {
            auto const p1_05 = Number(105, -2);
            // First interval slot price
            if (*timeSlot == 0)
                return pricePurchased * p1_05 + minSlotPrice;
            // Other intervals slot price
            return pricePurchased * p1_05 * (1 - power(fractionUsed, 60)) +
                minSlotPrice;
        }();

        auto const payPrice = getPayPrice(computedPrice);

        if (!payPrice)
            return {payPrice.error(), false};

        // Refund the previous owner. If the time slot is 0 then
        // the owner is refunded 95% of the amount.
        auto const refund = fractionRemaining * pricePurchased;
        if (refund > *payPrice)
        {
            // This error case should never occur.
            JLOG(ctx_.journal.fatal()) << "AMM Bid: refund exceeds payPrice "
                                       << refund << " " << *payPrice;
            return {tecINTERNAL, false};
        }
        res = accountSend(
            sb,
            account_,
            auctionSlot[sfAccount],
            toSTAmount(lpTokens.issue(), refund),
            ctx_.journal);
        if (res != tesSUCCESS)
        {
            JLOG(ctx_.journal.debug()) << "AMM Bid: failed to refund.";
            return {res, false};
        }

        auto const burn = *payPrice - refund;
        res = updateSlot(discountedFee, *payPrice, burn);
    }

    return {res, res == tesSUCCESS};
}

TER
AMMBid::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    auto const result = applyBid(ctx_, sb, account_, j_);
    if (result.second)
        sb.apply(ctx_.rawView());

    return result.first;
}

}  // namespace ripple
