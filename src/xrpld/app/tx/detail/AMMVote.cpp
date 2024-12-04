//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <xrpld/app/tx/detail/AMMVote.h>

#include <xrpld/app/misc/AMMHelpers.h>
#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
AMMVote::preflight(PreflightContext const& ctx)
{
    if (!ammEnabled(ctx.rules))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (auto const res = invalidAMMAssetPair(ctx.tx[sfAsset], ctx.tx[sfAsset2]))
    {
        JLOG(ctx.j.debug()) << "AMM Vote: invalid asset pair.";
        return res;
    }

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.debug()) << "AMM Vote: invalid flags.";
        return temINVALID_FLAG;
    }

    if (ctx.tx[sfTradingFee] > TRADING_FEE_THRESHOLD)
    {
        JLOG(ctx.j.debug()) << "AMM Vote: invalid trading fee.";
        return temBAD_FEE;
    }

    return preflight2(ctx);
}

TER
AMMVote::preclaim(PreclaimContext const& ctx)
{
    if (auto const ammSle =
            ctx.view.read(keylet::amm(ctx.tx[sfAsset], ctx.tx[sfAsset2]));
        !ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Vote: Invalid asset pair.";
        return terNO_AMM;
    }
    else if (ammSle->getFieldAmount(sfLPTokenBalance) == beast::zero)
        return tecAMM_EMPTY;
    else if (auto const lpTokensNew =
                 ammLPHolds(ctx.view, *ammSle, ctx.tx[sfAccount], ctx.j);
             lpTokensNew == beast::zero)
    {
        JLOG(ctx.j.debug()) << "AMM Vote: account is not LP.";
        return tecAMM_INVALID_TOKENS;
    }

    return tesSUCCESS;
}

static std::pair<TER, bool>
applyVote(
    ApplyContext& ctx_,
    Sandbox& sb,
    AccountID const& account_,
    beast::Journal j_)
{
    auto const feeNew = ctx_.tx[sfTradingFee];
    auto ammSle = sb.peek(keylet::amm(ctx_.tx[sfAsset], ctx_.tx[sfAsset2]));
    if (!ammSle)
        return {tecINTERNAL, false};
    STAmount const lptAMMBalance = (*ammSle)[sfLPTokenBalance];
    auto const lpTokensNew = ammLPHolds(sb, *ammSle, account_, ctx_.journal);
    std::optional<STAmount> minTokens;
    std::size_t minPos{0};
    AccountID minAccount{0};
    std::uint32_t minFee{0};
    STArray updatedVoteSlots;
    Number num{0};
    Number den{0};
    // Account already has vote entry
    bool foundAccount = false;

    // Iterate over the current vote entries and update each entry
    // per current total tokens balance and each LP tokens balance.
    // Find the entry with the least tokens and whether the account
    // has the vote entry.
    for (auto const& entry : ammSle->getFieldArray(sfVoteSlots))
    {
        auto const account = entry[sfAccount];
        auto lpTokens = ammLPHolds(sb, *ammSle, account, ctx_.journal);
        if (lpTokens == beast::zero)
        {
            JLOG(j_.debug())
                << "AMMVote::applyVote, account " << account << " is not LP";
            continue;
        }
        auto feeVal = entry[sfTradingFee];
        STObject newEntry = STObject::makeInnerObject(sfVoteEntry);
        // The account already has the vote entry.
        if (account == account_)
        {
            lpTokens = lpTokensNew;
            feeVal = feeNew;
            foundAccount = true;
        }
        // Keep running numerator/denominator to calculate the updated fee.
        num += feeVal * lpTokens;
        den += lpTokens;
        newEntry.setAccountID(sfAccount, account);
        if (feeVal != 0)
            newEntry.setFieldU16(sfTradingFee, feeVal);
        newEntry.setFieldU32(
            sfVoteWeight,
            static_cast<std::int64_t>(
                Number(lpTokens) * VOTE_WEIGHT_SCALE_FACTOR / lptAMMBalance));

        // Find an entry with the least tokens/fee. Make the order deterministic
        // if the tokens/fees are equal.
        if (!minTokens ||
            (lpTokens < *minTokens ||
             (lpTokens == *minTokens &&
              (feeVal < minFee || (feeVal == minFee && account < minAccount)))))
        {
            minTokens = lpTokens;
            minPos = updatedVoteSlots.size();
            minAccount = account;
            minFee = feeVal;
        }
        updatedVoteSlots.push_back(std::move(newEntry));
    }

    // The account doesn't have the vote entry.
    if (!foundAccount)
    {
        auto update = [&](std::optional<std::uint8_t> const& minPos =
                              std::nullopt) {
            STObject newEntry = STObject::makeInnerObject(sfVoteEntry);
            if (feeNew != 0)
                newEntry.setFieldU16(sfTradingFee, feeNew);
            newEntry.setFieldU32(
                sfVoteWeight,
                static_cast<std::int64_t>(
                    Number(lpTokensNew) * VOTE_WEIGHT_SCALE_FACTOR /
                    lptAMMBalance));
            newEntry.setAccountID(sfAccount, account_);
            num += feeNew * lpTokensNew;
            den += lpTokensNew;
            if (minPos)
                *(updatedVoteSlots.begin() + *minPos) = std::move(newEntry);
            else
                updatedVoteSlots.push_back(std::move(newEntry));
        };
        // Add new entry if the number of the vote entries
        // is less than Max.
        if (updatedVoteSlots.size() < VOTE_MAX_SLOTS)
            update();
        // Add the entry if the account has more tokens than
        // the least token holder or same tokens and higher fee.
        else if (
            lpTokensNew > *minTokens ||
            (lpTokensNew == *minTokens && feeNew > minFee))
        {
            auto const entry = updatedVoteSlots.begin() + minPos;
            // Remove the least token vote entry.
            num -= Number((*entry)[~sfTradingFee].value_or(0)) * *minTokens;
            den -= *minTokens;
            update(minPos);
        }
        // All slots are full and the account does not hold more LPTokens.
        // Update anyway to refresh the slots.
        else
        {
            JLOG(j_.debug()) << "AMMVote::applyVote, insufficient tokens to "
                                "override other votes";
        }
    }

    ASSERT(
        !ctx_.view().rules().enabled(fixInnerObjTemplate) ||
            ammSle->isFieldPresent(sfAuctionSlot),
        "ripple::applyVote : has auction slot");

    // Update the vote entries and the trading/discounted fee.
    ammSle->setFieldArray(sfVoteSlots, updatedVoteSlots);
    if (auto const fee = static_cast<std::int64_t>(num / den))
    {
        ammSle->setFieldU16(sfTradingFee, fee);
        if (ammSle->isFieldPresent(sfAuctionSlot))
        {
            auto& auctionSlot = ammSle->peekFieldObject(sfAuctionSlot);
            if (auto const discountedFee =
                    fee / AUCTION_SLOT_DISCOUNTED_FEE_FRACTION)
                auctionSlot.setFieldU16(sfDiscountedFee, discountedFee);
            else if (auctionSlot.isFieldPresent(sfDiscountedFee))
                auctionSlot.makeFieldAbsent(sfDiscountedFee);
        }
    }
    else
    {
        if (ammSle->isFieldPresent(sfTradingFee))
            ammSle->makeFieldAbsent(sfTradingFee);
        if (ammSle->isFieldPresent(sfAuctionSlot))
        {
            auto& auctionSlot = ammSle->peekFieldObject(sfAuctionSlot);
            if (auctionSlot.isFieldPresent(sfDiscountedFee))
                auctionSlot.makeFieldAbsent(sfDiscountedFee);
        }
    }
    sb.update(ammSle);

    return {tesSUCCESS, true};
}

TER
AMMVote::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    auto const result = applyVote(ctx_, sb, account_, j_);
    if (result.second)
        sb.apply(ctx_.rawView());

    return result.first;
}

}  // namespace ripple
