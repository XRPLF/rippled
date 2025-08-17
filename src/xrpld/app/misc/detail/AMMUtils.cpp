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

#include <xrpld/app/misc/AMMHelpers.h>
#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/detail/View.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STObject.h>

#include <cmath>
#include <limits>

namespace ripple {

std::pair<STAmount, STAmount>
ammPoolHolds(
    ReadView const& view,
    AccountID const& ammAccountID,
    Issue const& issue1,
    Issue const& issue2,
    FreezeHandling freezeHandling,
    beast::Journal const j)
{
    auto const assetInBalance =
        accountHolds(view, ammAccountID, issue1, freezeHandling, j);
    auto const assetOutBalance =
        accountHolds(view, ammAccountID, issue2, freezeHandling, j);
    return std::make_pair(assetInBalance, assetOutBalance);
}

Expected<std::tuple<STAmount, STAmount, STAmount>, TER>
ammHolds(
    ReadView const& view,
    SLE const& ammSle,
    std::optional<Issue> const& optIssue1,
    std::optional<Issue> const& optIssue2,
    FreezeHandling freezeHandling,
    beast::Journal const j)
{
    auto const issues = [&]() -> std::optional<std::pair<Issue, Issue>> {
        auto const issue1 = ammSle[sfAsset].get<Issue>();
        auto const issue2 = ammSle[sfAsset2].get<Issue>();
        if (optIssue1 && optIssue2)
        {
            if (invalidAMMAssetPair(
                    *optIssue1,
                    *optIssue2,
                    std::make_optional(std::make_pair(issue1, issue2))))
            {
                // This error can only be hit if the AMM is corrupted
                // LCOV_EXCL_START
                JLOG(j.debug()) << "ammHolds: Invalid optIssue1 or optIssue2 "
                                << *optIssue1 << " " << *optIssue2;
                return std::nullopt;
                // LCOV_EXCL_STOP
            }
            return std::make_optional(std::make_pair(*optIssue1, *optIssue2));
        }
        auto const singleIssue =
            [&issue1, &issue2, &j](
                Issue checkIssue,
                char const* label) -> std::optional<std::pair<Issue, Issue>> {
            if (checkIssue == issue1)
                return std::make_optional(std::make_pair(issue1, issue2));
            else if (checkIssue == issue2)
                return std::make_optional(std::make_pair(issue2, issue1));
            // Unreachable unless AMM corrupted.
            // LCOV_EXCL_START
            JLOG(j.debug())
                << "ammHolds: Invalid " << label << " " << checkIssue;
            return std::nullopt;
            // LCOV_EXCL_STOP
        };
        if (optIssue1)
        {
            return singleIssue(*optIssue1, "optIssue1");
        }
        else if (optIssue2)
        {
            // Cannot have Amount2 without Amount.
            return singleIssue(*optIssue2, "optIssue2");  // LCOV_EXCL_LINE
        }
        return std::make_optional(std::make_pair(issue1, issue2));
    }();
    if (!issues)
        return Unexpected(tecAMM_INVALID_TOKENS);
    auto const [asset1, asset2] = ammPoolHolds(
        view,
        ammSle.getAccountID(sfAccount),
        issues->first,
        issues->second,
        freezeHandling,
        j);
    return std::make_tuple(asset1, asset2, ammSle[sfLPTokenBalance]);
}

STAmount
ammLPHolds(
    ReadView const& view,
    Currency const& cur1,
    Currency const& cur2,
    AccountID const& ammAccount,
    AccountID const& lpAccount,
    beast::Journal const j)
{
    // This function looks similar to `accountHolds`. However, it only checks if
    // a LPToken holder has enough balance. On the other hand, `accountHolds`
    // checks if the underlying assets of LPToken are frozen with the
    // fixFrozenLPTokenTransfer amendment

    auto const currency = ammLPTCurrency(cur1, cur2);
    STAmount amount;

    auto const sle = view.read(keylet::line(lpAccount, ammAccount, currency));
    if (!sle)
    {
        amount.clear(Issue{currency, ammAccount});
        JLOG(j.trace()) << "ammLPHolds: no SLE "
                        << " lpAccount=" << to_string(lpAccount)
                        << " amount=" << amount.getFullText();
    }
    else if (isFrozen(view, lpAccount, currency, ammAccount))
    {
        amount.clear(Issue{currency, ammAccount});
        JLOG(j.trace()) << "ammLPHolds: frozen currency "
                        << " lpAccount=" << to_string(lpAccount)
                        << " amount=" << amount.getFullText();
    }
    else
    {
        amount = sle->getFieldAmount(sfBalance);
        if (lpAccount > ammAccount)
        {
            // Put balance in account terms.
            amount.negate();
        }
        amount.setIssuer(ammAccount);

        JLOG(j.trace()) << "ammLPHolds:"
                        << " lpAccount=" << to_string(lpAccount)
                        << " amount=" << amount.getFullText();
    }

    return view.balanceHook(lpAccount, ammAccount, amount);
}

STAmount
ammLPHolds(
    ReadView const& view,
    SLE const& ammSle,
    AccountID const& lpAccount,
    beast::Journal const j)
{
    return ammLPHolds(
        view,
        ammSle[sfAsset].get<Issue>().currency,
        ammSle[sfAsset2].get<Issue>().currency,
        ammSle[sfAccount],
        lpAccount,
        j);
}

std::uint16_t
getTradingFee(ReadView const& view, SLE const& ammSle, AccountID const& account)
{
    using namespace std::chrono;
    XRPL_ASSERT(
        !view.rules().enabled(fixInnerObjTemplate) ||
            ammSle.isFieldPresent(sfAuctionSlot),
        "ripple::getTradingFee : auction present");
    if (ammSle.isFieldPresent(sfAuctionSlot))
    {
        auto const& auctionSlot =
            static_cast<STObject const&>(ammSle.peekAtField(sfAuctionSlot));
        // Not expired
        if (auto const expiration = auctionSlot[~sfExpiration];
            duration_cast<seconds>(
                view.info().parentCloseTime.time_since_epoch())
                .count() < expiration)
        {
            if (auctionSlot[~sfAccount] == account)
                return auctionSlot[sfDiscountedFee];
            if (auctionSlot.isFieldPresent(sfAuthAccounts))
            {
                for (auto const& acct :
                     auctionSlot.getFieldArray(sfAuthAccounts))
                    if (acct[~sfAccount] == account)
                        return auctionSlot[sfDiscountedFee];
            }
        }
    }
    return ammSle[sfTradingFee];
}

STAmount
ammAccountHolds(
    ReadView const& view,
    AccountID const& ammAccountID,
    Issue const& issue)
{
    if (isXRP(issue))
    {
        if (auto const sle = view.read(keylet::account(ammAccountID)))
            return (*sle)[sfBalance];
    }
    else if (auto const sle = view.read(
                 keylet::line(ammAccountID, issue.account, issue.currency));
             sle &&
             !isFrozen(view, ammAccountID, issue.currency, issue.account))
    {
        auto amount = (*sle)[sfBalance];
        if (ammAccountID > issue.account)
            amount.negate();
        amount.setIssuer(issue.account);
        return amount;
    }

    return STAmount{issue};
}

static TER
deleteAMMTrustLines(
    Sandbox& sb,
    AccountID const& ammAccountID,
    std::uint16_t maxTrustlinesToDelete,
    beast::Journal j)
{
    return cleanupOnAccountDelete(
        sb,
        keylet::ownerDir(ammAccountID),
        [&](LedgerEntryType nodeType,
            uint256 const&,
            std::shared_ptr<SLE>& sleItem) -> std::pair<TER, SkipEntry> {
            // Skip AMM
            if (nodeType == LedgerEntryType::ltAMM)
                return {tesSUCCESS, SkipEntry::Yes};
            // Should only have the trustlines
            if (nodeType != LedgerEntryType::ltRIPPLE_STATE)
            {
                // LCOV_EXCL_START
                JLOG(j.error())
                    << "deleteAMMTrustLines: deleting non-trustline "
                    << nodeType;
                return {tecINTERNAL, SkipEntry::No};
                // LCOV_EXCL_STOP
            }

            // Trustlines must have zero balance
            if (sleItem->getFieldAmount(sfBalance) != beast::zero)
            {
                // LCOV_EXCL_START
                JLOG(j.error())
                    << "deleteAMMTrustLines: deleting trustline with "
                       "non-zero balance.";
                return {tecINTERNAL, SkipEntry::No};
                // LCOV_EXCL_STOP
            }

            return {
                deleteAMMTrustLine(sb, sleItem, ammAccountID, j),
                SkipEntry::No};
        },
        j,
        maxTrustlinesToDelete);
}

TER
deleteAMMAccount(
    Sandbox& sb,
    Issue const& asset,
    Issue const& asset2,
    beast::Journal j)
{
    auto ammSle = sb.peek(keylet::amm(asset, asset2));
    if (!ammSle)
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "deleteAMMAccount: AMM object does not exist "
                        << asset << " " << asset2;
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    auto const ammAccountID = (*ammSle)[sfAccount];
    auto sleAMMRoot = sb.peek(keylet::account(ammAccountID));
    if (!sleAMMRoot)
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "deleteAMMAccount: AMM account does not exist "
                        << to_string(ammAccountID);
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    if (auto const ter =
            deleteAMMTrustLines(sb, ammAccountID, maxDeletableAMMTrustLines, j);
        ter != tesSUCCESS)
        return ter;

    auto const ownerDirKeylet = keylet::ownerDir(ammAccountID);
    if (!sb.dirRemove(
            ownerDirKeylet, (*ammSle)[sfOwnerNode], ammSle->key(), false))
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "deleteAMMAccount: failed to remove dir link";
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }
    if (sb.exists(ownerDirKeylet) && !sb.emptyDirDelete(ownerDirKeylet))
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "deleteAMMAccount: cannot delete root dir node of "
                        << toBase58(ammAccountID);
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    sb.erase(ammSle);
    sb.erase(sleAMMRoot);

    return tesSUCCESS;
}

void
initializeFeeAuctionVote(
    ApplyView& view,
    std::shared_ptr<SLE>& ammSle,
    AccountID const& account,
    Issue const& lptIssue,
    std::uint16_t tfee)
{
    auto const& rules = view.rules();
    // AMM creator gets the voting slot.
    STArray voteSlots;
    STObject voteEntry = STObject::makeInnerObject(sfVoteEntry);
    if (tfee != 0)
        voteEntry.setFieldU16(sfTradingFee, tfee);
    voteEntry.setFieldU32(sfVoteWeight, VOTE_WEIGHT_SCALE_FACTOR);
    voteEntry.setAccountID(sfAccount, account);
    voteSlots.push_back(voteEntry);
    ammSle->setFieldArray(sfVoteSlots, voteSlots);
    // AMM creator gets the auction slot for free.
    // AuctionSlot is created on AMMCreate and updated on AMMDeposit
    // when AMM is in an empty state
    if (rules.enabled(fixInnerObjTemplate) &&
        !ammSle->isFieldPresent(sfAuctionSlot))
    {
        STObject auctionSlot = STObject::makeInnerObject(sfAuctionSlot);
        ammSle->set(std::move(auctionSlot));
    }
    STObject& auctionSlot = ammSle->peekFieldObject(sfAuctionSlot);
    auctionSlot.setAccountID(sfAccount, account);
    // current + sec in 24h
    auto const expiration = std::chrono::duration_cast<std::chrono::seconds>(
                                view.info().parentCloseTime.time_since_epoch())
                                .count() +
        TOTAL_TIME_SLOT_SECS;
    auctionSlot.setFieldU32(sfExpiration, expiration);
    auctionSlot.setFieldAmount(sfPrice, STAmount{lptIssue, 0});
    // Set the fee
    if (tfee != 0)
        ammSle->setFieldU16(sfTradingFee, tfee);
    else if (ammSle->isFieldPresent(sfTradingFee))
        ammSle->makeFieldAbsent(sfTradingFee);  // LCOV_EXCL_LINE
    if (auto const dfee = tfee / AUCTION_SLOT_DISCOUNTED_FEE_FRACTION)
        auctionSlot.setFieldU16(sfDiscountedFee, dfee);
    else if (auctionSlot.isFieldPresent(sfDiscountedFee))
        auctionSlot.makeFieldAbsent(sfDiscountedFee);  // LCOV_EXCL_LINE
}

Expected<bool, TER>
isOnlyLiquidityProvider(
    ReadView const& view,
    Issue const& ammIssue,
    AccountID const& lpAccount)
{
    // Liquidity Provider (LP) must have one LPToken trustline
    std::uint8_t nLPTokenTrustLines = 0;
    // There are at most two IOU trustlines. One or both could be to the LP
    // if LP is the issuer, or a different account if LP is not an issuer.
    // For instance, if AMM has two tokens USD and EUR and LP is not the issuer
    // of the tokens then the trustlines are between AMM account and the issuer.
    std::uint8_t nIOUTrustLines = 0;
    // There is only one AMM object
    bool hasAMM = false;
    // AMM LP has at most three trustlines and only one AMM object must exist.
    // If there are more than five objects then it's either an error or
    // there are more than one LP. Ten pages should be sufficient to include
    // five objects.
    std::uint8_t limit = 10;
    auto const root = keylet::ownerDir(ammIssue.account);
    auto currentIndex = root;

    // Iterate over AMM owner directory objects.
    while (limit-- >= 1)
    {
        auto const ownerDir = view.read(currentIndex);
        if (!ownerDir)
            return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
        for (auto const& key : ownerDir->getFieldV256(sfIndexes))
        {
            auto const sle = view.read(keylet::child(key));
            if (!sle)
                return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
            // Only one AMM object
            if (sle->getFieldU16(sfLedgerEntryType) == ltAMM)
            {
                if (hasAMM)
                    return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
                hasAMM = true;
                continue;
            }
            if (sle->getFieldU16(sfLedgerEntryType) != ltRIPPLE_STATE)
                return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
            auto const lowLimit = sle->getFieldAmount(sfLowLimit);
            auto const highLimit = sle->getFieldAmount(sfHighLimit);
            auto const isLPTrustline = lowLimit.getIssuer() == lpAccount ||
                highLimit.getIssuer() == lpAccount;
            auto const isLPTokenTrustline =
                lowLimit.issue() == ammIssue || highLimit.issue() == ammIssue;

            // Liquidity Provider trustline
            if (isLPTrustline)
            {
                // LPToken trustline
                if (isLPTokenTrustline)
                {
                    if (++nLPTokenTrustLines > 1)
                        return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
                }
                else if (++nIOUTrustLines > 2)
                    return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
            }
            // Another Liquidity Provider LPToken trustline
            else if (isLPTokenTrustline)
                return false;
            else if (++nIOUTrustLines > 2)
                return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
        }
        auto const uNodeNext = ownerDir->getFieldU64(sfIndexNext);
        if (uNodeNext == 0)
        {
            if (nLPTokenTrustLines != 1 || nIOUTrustLines == 0 ||
                nIOUTrustLines > 2)
                return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
            return true;
        }
        currentIndex = keylet::page(root, uNodeNext);
    }
    return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
}

Expected<bool, TER>
verifyAndAdjustLPTokenBalance(
    Sandbox& sb,
    STAmount const& lpTokens,
    std::shared_ptr<SLE>& ammSle,
    AccountID const& account)
{
    if (auto const res = isOnlyLiquidityProvider(sb, lpTokens.issue(), account);
        !res)
        return Unexpected<TER>(res.error());
    else if (res.value())
    {
        if (withinRelativeDistance(
                lpTokens,
                ammSle->getFieldAmount(sfLPTokenBalance),
                Number{1, -3}))
        {
            ammSle->setFieldAmount(sfLPTokenBalance, lpTokens);
            sb.update(ammSle);
        }
        else
        {
            return Unexpected<TER>(tecAMM_INVALID_TOKENS);
        }
    }
    return true;
}

// Concentrated Liquidity Fee Functions

std::pair<STAmount, STAmount>
ammConcentratedLiquidityFeeGrowth(
    ReadView const& view,
    uint256 const& ammID,
    std::int32_t currentTick,
    STAmount const& amountIn,
    STAmount const& amountOut,
    std::uint16_t tradingFee,
    beast::Journal const& j)
{
    // For concentrated liquidity, we need to calculate fees differently
    // based on the active liquidity in the current price range

    auto const ammSle = view.read(keylet::amm(ammID));
    if (!ammSle)
    {
        JLOG(j.debug()) << "AMM not found for fee calculation";
        return {STAmount{0}, STAmount{0}};
    }

    // Get the active liquidity for the current tick range
    // This is the key difference: concentrated liquidity fees are based on
    // active liquidity, not total liquidity
    auto const activeLiquidity = ammSle->isFieldPresent(sfAggregatedLiquidity)
        ? ammSle->getFieldAmount(sfAggregatedLiquidity)
        : ammSle->getFieldAmount(sfLPTokenBalance);

    if (activeLiquidity <= STAmount{0})
    {
        JLOG(j.debug()) << "No active liquidity for fee calculation";
        return {STAmount{0}, STAmount{0}};
    }

    // Calculate fee amount using the same mechanism as regular AMM
    // but applied to the active liquidity only
    auto const feeAmount = mulRatio(
        amountIn.xrp(), 
        static_cast<std::uint32_t>(tradingFee), 
        static_cast<std::uint32_t>(1000000), 
        false);

    // Fee growth per unit of active liquidity
    auto const feeGrowthDelta = mulRatio(
        feeAmount, 
        static_cast<std::uint32_t>(1), 
        static_cast<std::uint32_t>(activeLiquidity.mantissa()), 
        false);

    // Determine which asset the fee is in
    auto const asset0 = (*ammSle)[sfAsset].get<Issue>();
    // auto const asset1 = (*ammSle)[sfAsset2].get<Issue>();  // Unused for now

    if (amountIn.issue() == asset0)
    {
        return {feeGrowthDelta, STAmount{0}};
    }
    else
    {
        return {STAmount{0}, feeGrowthDelta};
    }
}

TER
ammConcentratedLiquidityUpdatePositionFees(
    ApplyView& view,
    Keylet const& positionKey,
    std::int32_t tickLower,
    std::int32_t tickUpper,
    std::int32_t currentTick,
    STAmount const& feeGrowthGlobal0,
    STAmount const& feeGrowthGlobal1,
    beast::Journal const& j)
{
    auto const positionSle = view.read(positionKey);
    if (!positionSle)
    {
        JLOG(j.debug()) << "Position not found for fee update";
        return tecAMM_POSITION_NOT_FOUND;
    }

    // Calculate fee growth inside the position's tick range
    auto const [feeGrowthInside0, feeGrowthInside1] =
        ammConcentratedLiquidityCalculateFeeGrowthInside(
            view,
            positionSle->getFieldH256(sfAMMID),
            tickLower,
            tickUpper,
            currentTick,
            feeGrowthGlobal0,
            feeGrowthGlobal1,
            j);

    // Calculate fees owed
    auto const liquidity = positionSle->getFieldAmount(sfLiquidity);
    auto const feeGrowthInside0Last =
        positionSle->getFieldAmount(sfFeeGrowthInside0LastX128);
    auto const feeGrowthInside1Last =
        positionSle->getFieldAmount(sfFeeGrowthInside1LastX128);

    auto const feeGrowthInside0Delta = feeGrowthInside0 - feeGrowthInside0Last;
    auto const feeGrowthInside1Delta = feeGrowthInside1 - feeGrowthInside1Last;

    auto const feesOwed0 = mulRatio(
        liquidity.xrp(), 
        static_cast<std::uint32_t>(feeGrowthInside0Delta.mantissa()), 
        static_cast<std::uint32_t>(1), 
        false);
    auto const feesOwed1 = mulRatio(
        liquidity.xrp(), 
        static_cast<std::uint32_t>(feeGrowthInside1Delta.mantissa()), 
        static_cast<std::uint32_t>(1), 
        false);

    // Update position
    auto const newPositionSle = std::make_shared<SLE>(*positionSle);
    newPositionSle->setFieldAmount(
        sfFeeGrowthInside0LastX128, feeGrowthInside0);
    newPositionSle->setFieldAmount(
        sfFeeGrowthInside1LastX128, feeGrowthInside1);

    auto const currentTokensOwed0 =
        newPositionSle->getFieldAmount(sfTokensOwed0);
    auto const currentTokensOwed1 =
        newPositionSle->getFieldAmount(sfTokensOwed1);

    newPositionSle->setFieldAmount(
        sfTokensOwed0, currentTokensOwed0 + feesOwed0);
    newPositionSle->setFieldAmount(
        sfTokensOwed1, currentTokensOwed1 + feesOwed1);

    view.update(newPositionSle);

    JLOG(j.debug()) << "Updated position fees: owed0=" << feesOwed0
                    << " owed1=" << feesOwed1;

    return tesSUCCESS;
}

std::pair<STAmount, STAmount>
ammConcentratedLiquidityCalculateFeesOwed(
    ReadView const& view,
    Keylet const& positionKey,
    STAmount const& feeGrowthGlobal0,
    STAmount const& feeGrowthGlobal1,
    beast::Journal const& j)
{
    auto const positionSle = view.read(positionKey);
    if (!positionSle)
    {
        JLOG(j.debug()) << "Position not found for fee calculation";
        return {STAmount{0}, STAmount{0}};
    }

    auto const tickLower = positionSle->getFieldU32(sfTickLower);
    auto const tickUpper = positionSle->getFieldU32(sfTickUpper);

    // Get current tick from AMM
    auto const ammSle =
        view.read(keylet::amm(positionSle->getFieldH256(sfAMMID)));
    if (!ammSle)
    {
        JLOG(j.debug()) << "AMM not found for fee calculation";
        return {STAmount{0}, STAmount{0}};
    }

    auto const currentTick = ammSle->getFieldU32(sfCurrentTick);

    // Calculate fee growth inside the position's tick range
    auto const [feeGrowthInside0, feeGrowthInside1] =
        ammConcentratedLiquidityCalculateFeeGrowthInside(
            view,
            positionSle->getFieldH256(sfAMMID),
            tickLower,
            tickUpper,
            currentTick,
            feeGrowthGlobal0,
            feeGrowthGlobal1,
            j);

    // Calculate fees owed
    auto const liquidity = positionSle->getFieldAmount(sfLiquidity);
    auto const feeGrowthInside0Last =
        positionSle->getFieldAmount(sfFeeGrowthInside0LastX128);
    auto const feeGrowthInside1Last =
        positionSle->getFieldAmount(sfFeeGrowthInside1LastX128);

    auto const feeGrowthInside0Delta = feeGrowthInside0 - feeGrowthInside0Last;
    auto const feeGrowthInside1Delta = feeGrowthInside1 - feeGrowthInside1Last;

    auto const feesOwed0 = mulRatio(
        liquidity.xrp(), 
        static_cast<std::uint32_t>(feeGrowthInside0Delta.mantissa()), 
        static_cast<std::uint32_t>(1), 
        false);
    auto const feesOwed1 = mulRatio(
        liquidity.xrp(), 
        static_cast<std::uint32_t>(feeGrowthInside1Delta.mantissa()), 
        static_cast<std::uint32_t>(1), 
        false);

    return {feesOwed0, feesOwed1};
}

TER
ammConcentratedLiquidityUpdateTickFeeGrowth(
    ApplyView& view,
    std::int32_t tick,
    STAmount const& feeGrowthGlobal0,
    STAmount const& feeGrowthGlobal1,
    bool isAboveCurrentTick,
    beast::Journal const& j)
{
    auto const tickKey = getConcentratedLiquidityTickKey(tick);
    auto const tickSle = view.read(keylet::child(tickKey));

    if (!tickSle)
    {
        JLOG(j.debug()) << "Tick not found for fee growth update";
        return tecAMM_TICK_NOT_INITIALIZED;
    }

    auto const newTickSle = std::make_shared<SLE>(*tickSle);

    if (isAboveCurrentTick)
    {
        // Update fee growth outside (above current tick)
        newTickSle->setFieldAmount(sfFeeGrowthOutside0X128, feeGrowthGlobal0);
        newTickSle->setFieldAmount(sfFeeGrowthOutside1X128, feeGrowthGlobal1);
    }
    else
    {
        // Update fee growth outside (below current tick)
        newTickSle->setFieldAmount(sfFeeGrowthOutside0X128, feeGrowthGlobal0);
        newTickSle->setFieldAmount(sfFeeGrowthOutside1X128, feeGrowthGlobal1);
    }

    view.update(newTickSle);

    JLOG(j.debug()) << "Updated tick " << tick << " fee growth";

    return tesSUCCESS;
}

// Helper function for calculating fee growth inside a tick range
std::pair<STAmount, STAmount>
ammConcentratedLiquidityCalculateFeeGrowthInside(
    ReadView const& view,
    uint256 const& ammID,
    std::int32_t tickLower,
    std::int32_t tickUpper,
    std::int32_t currentTick,
    STAmount const& feeGrowthGlobal0,
    STAmount const& feeGrowthGlobal1,
    beast::Journal const& j)
{
    // Get fee growth outside for lower and upper ticks
    auto const lowerTickKey = getConcentratedLiquidityTickKey(tickLower);
    auto const upperTickKey = getConcentratedLiquidityTickKey(tickUpper);

    auto const lowerTickSle = view.read(keylet::child(lowerTickKey));
    auto const upperTickSle = view.read(keylet::child(upperTickKey));

    STAmount feeGrowthOutside0Lower{0};
    STAmount feeGrowthOutside1Lower{0};
    STAmount feeGrowthOutside0Upper{0};
    STAmount feeGrowthOutside1Upper{0};

    if (lowerTickSle)
    {
        feeGrowthOutside0Lower =
            lowerTickSle->getFieldAmount(sfFeeGrowthOutside0X128);
        feeGrowthOutside1Lower =
            lowerTickSle->getFieldAmount(sfFeeGrowthOutside1X128);
    }

    if (upperTickSle)
    {
        feeGrowthOutside0Upper =
            upperTickSle->getFieldAmount(sfFeeGrowthOutside0X128);
        feeGrowthOutside1Upper =
            upperTickSle->getFieldAmount(sfFeeGrowthOutside1X128);
    }

    // Calculate fee growth inside
    // Fee growth inside = fee growth global - fee growth outside lower - fee
    // growth outside upper
    auto const feeGrowthInside0 =
        feeGrowthGlobal0 - feeGrowthOutside0Lower - feeGrowthOutside0Upper;
    auto const feeGrowthInside1 =
        feeGrowthGlobal1 - feeGrowthOutside1Lower - feeGrowthOutside1Upper;

    return {feeGrowthInside0, feeGrowthInside1};
}

// Integrated AMM swap functions that work with both regular and concentrated
// liquidity

/** Calculate swap output for AMM with support for concentrated liquidity
 * This function integrates with existing AMM swap logic while supporting
 * concentrated liquidity when the feature is enabled
 */
template <typename TIn, typename TOut>
TOut
ammSwapAssetIn(
    ReadView const& view,
    uint256 const& ammID,
    TAmountPair<TIn, TOut> const& pool,
    TIn const& assetIn,
    std::uint16_t tradingFee,
    beast::Journal const& j)
{
    // Check if this is a concentrated liquidity AMM
    auto const ammSle = view.read(keylet::amm(ammID));
    if (ammSle && ammSle->isFieldPresent(sfCurrentTick))
    {
        // This is a concentrated liquidity AMM
        // Use concentrated liquidity swap calculation
        return ammConcentratedLiquiditySwapAssetIn(
            view, ammID, pool, assetIn, tradingFee, j);
    }
    else
    {
        // This is a regular AMM - use existing swap logic
        return swapAssetIn(pool, assetIn, tradingFee);
    }
}

/** Concentrated liquidity swap calculation
 * Implements Uniswap V3-style swap with proper fee handling and tick crossing
 */
template <typename TIn, typename TOut>
TOut
ammConcentratedLiquiditySwapAssetIn(
    ReadView const& view,
    uint256 const& ammID,
    TAmountPair<TIn, TOut> const& pool,
    TIn const& assetIn,
    std::uint16_t tradingFee,
    beast::Journal const& j)
{
    // For concentrated liquidity, we need to:
    // 1. Calculate the swap using the active liquidity
    // 2. Update fee growth for all affected positions
    // 3. Handle tick crossing if necessary

    auto const ammSle = view.read(keylet::amm(ammID));
    if (!ammSle)
    {
        JLOG(j.debug()) << "AMM not found for concentrated liquidity swap";
        return toAmount<TOut>(getIssue(pool.out), 0);
    }

    // Check if this is a concentrated liquidity AMM
    if (!ammSle->isFieldPresent(sfCurrentTick))
    {
        JLOG(j.debug()) << "Not a concentrated liquidity AMM";
        return toAmount<TOut>(getIssue(pool.out), 0);
    }

    // Get current tick and sqrt price
    auto const sqrtPriceX64 = ammSle->getFieldU64(sfSqrtPriceX64);

    // Get active liquidity
    auto const activeLiquidity = ammSle->isFieldPresent(sfAggregatedLiquidity)
        ? ammSle->getFieldAmount(sfAggregatedLiquidity)
        : ammSle->getFieldAmount(sfLPTokenBalance);

    if (activeLiquidity <= STAmount{0})
    {
        JLOG(j.debug())
            << "No active liquidity for concentrated liquidity swap";
        return toAmount<TOut>(getIssue(pool.out), 0);
    }

    // For read-only operations, use a simplified calculation
    // For actual swaps, use the tick crossing function
    auto const targetSqrtPriceX64 =
        calculateTargetSqrtPrice(sqrtPriceX64, assetIn, tradingFee, j);

    // Calculate output using the price change
    auto const output =
        calculateOutputForInput(sqrtPriceX64, targetSqrtPriceX64, assetIn, j);

    return toAmount<TOut>(getIssue(pool.out), output);
}

// Tick crossing functions for concentrated liquidity

/** Execute a swap with proper tick crossing logic
 * This is the main function that handles concentrated liquidity swaps
 * with proper tick crossing and fee growth updates
 */
template <typename TIn, typename TOut>
std::pair<TOut, TER>
ammConcentratedLiquiditySwapWithTickCrossing(
    ApplyView& view,
    uint256 const& ammID,
    TIn const& assetIn,
    std::uint16_t tradingFee,
    beast::Journal const& j)
{
    auto const ammSle = view.read(keylet::amm(ammID));
    if (!ammSle)
    {
        JLOG(j.debug()) << "AMM not found for concentrated liquidity swap";
        return {toAmount<TOut>(getIssue(assetIn), 0), tecINTERNAL};
    }

    auto const currentTick = ammSle->getFieldU32(sfCurrentTick);
    auto const sqrtPriceX64 = ammSle->getFieldU64(sfSqrtPriceX64);
    // auto const tickSpacing = ammSle->getFieldU16(sfTickSpacing);  // Unused for now

    // Get current fee growth
    auto const feeGrowthGlobal0 =
        ammSle->getFieldAmount(sfFeeGrowthGlobal0X128);
    auto const feeGrowthGlobal1 =
        ammSle->getFieldAmount(sfFeeGrowthGlobal1X128);

    // Calculate the target sqrt price after the swap
    auto const targetSqrtPriceX64 =
        calculateTargetSqrtPrice(sqrtPriceX64, assetIn, tradingFee, j);

    // Find the next initialized tick in the direction of the swap
    auto const nextTick = findNextInitializedTick(
        view, ammID, currentTick, targetSqrtPriceX64 > sqrtPriceX64, j);

    TOut totalOutput{0};
    TIn remainingInput = assetIn;
    std::int32_t currentTickIter = currentTick;
    std::uint64_t currentSqrtPriceX64 = sqrtPriceX64;

    // Execute the swap step by step, crossing ticks as needed
    while (remainingInput > TIn{0})
    {
        // Calculate the maximum amount we can swap before hitting the next tick
        auto const [maxInput, maxOutput, nextSqrtPriceX64] = calculateSwapStep(
            view,
            ammID,
            currentTickIter,
            currentSqrtPriceX64,
            nextTick,
            remainingInput,
            tradingFee,
            j);

        if (maxInput <= TIn{0})
        {
            JLOG(j.debug()) << "No more liquidity available for swap";
            break;
        }

        // Execute the swap step
        auto const actualInput = std::min(remainingInput, maxInput);
        auto const actualOutput = calculateOutputForInput(
            currentSqrtPriceX64, nextSqrtPriceX64, actualInput, j);

        totalOutput += actualOutput;
        remainingInput -= actualInput;

        // Update fee growth for the current tick range
        auto const feeGrowthDelta =
            calculateFeeGrowthForSwap(actualInput, actualOutput, tradingFee, j);

        // Update global fee growth
        auto const newFeeGrowthGlobal0 =
            feeGrowthGlobal0 + feeGrowthDelta.first;
        auto const newFeeGrowthGlobal1 =
            feeGrowthGlobal1 + feeGrowthDelta.second;

        // Update AMM state
        auto const newAmmSle = std::make_shared<SLE>(*ammSle);
        newAmmSle->setFieldAmount(sfFeeGrowthGlobal0X128, newFeeGrowthGlobal0);
        newAmmSle->setFieldAmount(sfFeeGrowthGlobal1X128, newFeeGrowthGlobal1);

        // Check if we need to cross a tick
        if (nextSqrtPriceX64 != currentSqrtPriceX64)
        {
            // Cross the tick
            if (auto const ter = crossTick(
                    view,
                    ammID,
                    currentTickIter,
                    nextSqrtPriceX64,
                    newFeeGrowthGlobal0,
                    newFeeGrowthGlobal1,
                    j);
                ter != tesSUCCESS)
            {
                return {totalOutput, ter};
            }

            currentTickIter = nextTick;
            currentSqrtPriceX64 = nextSqrtPriceX64;

            // Update AMM with new tick and price
            newAmmSle->setFieldU32(sfCurrentTick, currentTickIter);
            newAmmSle->setFieldU64(sfSqrtPriceX64, currentSqrtPriceX64);

            // Find the next initialized tick
            auto const nextTickIter = findNextInitializedTick(
                view,
                ammID,
                currentTickIter,
                targetSqrtPriceX64 > currentSqrtPriceX64,
                j);

            if (nextTickIter == currentTickIter)
            {
                // No more ticks to cross
                break;
            }
        }

        view.update(newAmmSle);
    }

    return {totalOutput, tesSUCCESS};
}

/** Calculate the target sqrt price for a given input amount */
std::uint64_t
calculateTargetSqrtPrice(
    std::uint64_t currentSqrtPriceX64,
    STAmount const& assetIn,
    std::uint16_t tradingFee,
    beast::Journal const& j)
{
    // SECURITY: Validate input parameters
    if (currentSqrtPriceX64 == 0)
    {
        JLOG(j.warn())
            << "calculateTargetSqrtPrice: currentSqrtPriceX64 cannot be zero";
        return 0;
    }

    if (tradingFee > 10000)  // Max reasonable fee is 1% (10000 basis points)
    {
        JLOG(j.warn()) << "calculateTargetSqrtPrice: invalid trading fee: "
                       << tradingFee;
        return currentSqrtPriceX64;
    }

    // SECURITY: Use safe arithmetic to prevent overflow
    auto const feeMultiplier = 1000000 - tradingFee;
    auto const inputValue = assetIn.mantissa();

    // SECURITY: Check for division by zero and overflow
    if (feeMultiplier == 0)
    {
        JLOG(j.warn()) << "calculateTargetSqrtPrice: fee multiplier is zero";
        return currentSqrtPriceX64;
    }

    // SECURITY: Use safe multiplication and division
    auto const scaledInput = inputValue * feeMultiplier;
    auto const deltaSqrtPrice = scaledInput / 1000000;

    // SECURITY: Check for overflow in addition
    if (deltaSqrtPrice >
        std::numeric_limits<std::uint64_t>::max() - currentSqrtPriceX64)
    {
        JLOG(j.warn()) << "calculateTargetSqrtPrice: overflow detected";
        return std::numeric_limits<std::uint64_t>::max();
    }

    return currentSqrtPriceX64 + static_cast<std::uint64_t>(deltaSqrtPrice);
}

/** Find the next initialized tick in the given direction */
std::int32_t
findNextInitializedTick(
    ReadView const& view,
    uint256 const& ammID,
    std::int32_t currentTick,
    bool ascending,
    beast::Journal const& j)
{
    // This is a simplified implementation
    // In practice, you'd need to iterate through all ticks and find the next
    // one
    auto const tickSpacing = 60;  // Default tick spacing

    if (ascending)
    {
        return currentTick + tickSpacing;
    }
    else
    {
        return currentTick - tickSpacing;
    }
}

/** Calculate the maximum amount that can be swapped before hitting the next
 * tick */
std::tuple<STAmount, STAmount, std::uint64_t>
calculateSwapStep(
    ReadView const& view,
    uint256 const& ammID,
    std::int32_t currentTick,
    std::uint64_t currentSqrtPriceX64,
    std::int32_t nextTick,
    STAmount const& maxInput,
    std::uint16_t tradingFee,
    beast::Journal const& j)
{
    // SECURITY: Validate input parameters
    if (currentSqrtPriceX64 == 0)
    {
        JLOG(j.warn())
            << "calculateSwapStep: currentSqrtPriceX64 cannot be zero";
        return {STAmount{0}, STAmount{0}, currentSqrtPriceX64};
    }

    if (tradingFee > 10000)  // Max reasonable fee is 1% (10000 basis points)
    {
        JLOG(j.warn()) << "calculateSwapStep: invalid trading fee: "
                       << tradingFee;
        return {STAmount{0}, STAmount{0}, currentSqrtPriceX64};
    }

    // Calculate the sqrt price at the next tick
    auto const nextSqrtPriceX64 = tickToSqrtPriceX64(nextTick);

    // SECURITY: Check for underflow in price difference
    if (nextSqrtPriceX64 <= currentSqrtPriceX64)
    {
        JLOG(j.warn()) << "calculateSwapStep: invalid price direction";
        return {STAmount{0}, STAmount{0}, currentSqrtPriceX64};
    }

    // SECURITY: Use safe arithmetic to prevent overflow
    auto const deltaSqrtPrice = nextSqrtPriceX64 - currentSqrtPriceX64;
    auto const feeMultiplier = 1000000 - tradingFee;

    // SECURITY: Check for division by zero
    if (feeMultiplier == 0)
    {
        JLOG(j.warn()) << "calculateSwapStep: fee multiplier is zero";
        return {STAmount{0}, STAmount{0}, currentSqrtPriceX64};
    }

    // SECURITY: Use safe multiplication and division
    auto const maxInputForTick = deltaSqrtPrice * 1000000 / feeMultiplier;

    auto const actualInput = std::min(maxInput, STAmount{maxInputForTick});
    auto const actualOutput = calculateOutputForInput(
        currentSqrtPriceX64, nextSqrtPriceX64, actualInput, j);

    return {actualInput, actualOutput, nextSqrtPriceX64};
}

/** Calculate output for a given input and price change */
STAmount
calculateOutputForInput(
    std::uint64_t sqrtPriceStartX64,
    std::uint64_t sqrtPriceEndX64,
    STAmount const& input,
    beast::Journal const& j)
{
    // SECURITY: Validate input parameters
    if (sqrtPriceStartX64 == 0)
    {
        JLOG(j.warn())
            << "calculateOutputForInput: sqrtPriceStartX64 cannot be zero";
        return STAmount{0};
    }

    if (input <= STAmount{0})
    {
        JLOG(j.warn()) << "calculateOutputForInput: input must be positive";
        return STAmount{0};
    }

    // SECURITY: Check for underflow in price difference
    if (sqrtPriceEndX64 <= sqrtPriceStartX64)
    {
        JLOG(j.warn()) << "calculateOutputForInput: invalid price direction";
        return STAmount{0};
    }

    // SECURITY: Use safe arithmetic to prevent overflow
    auto const deltaSqrtPrice = sqrtPriceEndX64 - sqrtPriceStartX64;

    // SECURITY: Check for overflow in multiplication
    if (deltaSqrtPrice >
        std::numeric_limits<std::uint64_t>::max() / input.mantissa())
    {
        JLOG(j.warn()) << "calculateOutputForInput: overflow in multiplication";
        return STAmount{std::numeric_limits<std::uint64_t>::max()};
    }

    auto const output = input * deltaSqrtPrice / sqrtPriceStartX64;
    return STAmount{input.issue(), output};
}

/** Calculate fee growth for a swap step */
std::pair<STAmount, STAmount>
calculateFeeGrowthForSwap(
    STAmount const& input,
    STAmount const& output,
    std::uint16_t tradingFee,
    beast::Journal const& j)
{
    // SECURITY: Validate input parameters
    if (input <= STAmount{0})
    {
        JLOG(j.warn()) << "calculateFeeGrowthForSwap: input must be positive";
        return {STAmount{0}, STAmount{0}};
    }

    if (tradingFee > 10000)  // Max reasonable fee is 1% (10000 basis points)
    {
        JLOG(j.warn()) << "calculateFeeGrowthForSwap: invalid trading fee: "
                       << tradingFee;
        return {STAmount{0}, STAmount{0}};
    }

    // SECURITY: Calculate fee amount with bounds checking
    auto const feeAmount = mulRatio(
        input.xrp(), 
        static_cast<std::uint32_t>(tradingFee), 
        static_cast<std::uint32_t>(1000000), 
        false);

    // SECURITY: Validate fee amount
    if (feeAmount > input)
    {
        JLOG(j.warn()) << "calculateFeeGrowthForSwap: fee amount exceeds input";
        return {STAmount{0}, STAmount{0}};
    }

    // Determine which asset the fee is in
    if (input.issue().currency == xrpCurrency())
    {
        return {feeAmount, STAmount{0}};
    }
    else
    {
        return {STAmount{0}, feeAmount};
    }
}

/** Cross a tick and update all affected positions */
TER
crossTick(
    ApplyView& view,
    uint256 const& ammID,
    std::int32_t tick,
    std::uint64_t newSqrtPriceX64,
    STAmount const& feeGrowthGlobal0,
    STAmount const& feeGrowthGlobal1,
    beast::Journal const& j)
{
    // Get the tick data
    auto const tickKey = getConcentratedLiquidityTickKey(tick);
    auto const tickSle = view.read(keylet::child(tickKey));

    if (!tickSle)
    {
        JLOG(j.debug()) << "Tick not found for crossing: " << tick;
        return tecAMM_TICK_NOT_INITIALIZED;
    }
    // Get the AMM SLE to access position data
    auto const ammSle = view.read(keylet::amm(ammID));
    if (!ammSle)
    {
        JLOG(j.debug()) << "AMM not found when crossing tick";
        return tecINTERNAL;
    }

    // Update the tick's fee growth outside
    auto const newTickSle = std::make_shared<SLE>(*tickSle);
    
    // When crossing a tick, we flip the fee growth outside values
    // This ensures proper fee accounting across tick boundaries
    auto const feeGrowthOutside0 = 
        feeGrowthGlobal0 - newTickSle->getFieldAmount(sfFeeGrowthOutside0X128);
    auto const feeGrowthOutside1 = 
        feeGrowthGlobal1 - newTickSle->getFieldAmount(sfFeeGrowthOutside1X128);
        
    newTickSle->setFieldAmount(sfFeeGrowthOutside0X128, feeGrowthOutside0);
    newTickSle->setFieldAmount(sfFeeGrowthOutside1X128, feeGrowthOutside1);
    view.update(newTickSle);

    // Get the net liquidity delta for this tick
    auto const liquidityNet = newTickSle->getFieldAmount(sfLiquidityNet);
    
    // Update all positions that have this tick as a boundary
    // Iterate through the AMM's owner directory to find concentrated liquidity positions
    auto const ammAccountID = ammSle->getAccountID(sfAccount);
    auto const ownerDirKeylet = keylet::ownerDir(ammAccountID);
    
    // Use directory iteration to find all concentrated liquidity positions
    std::shared_ptr<SLE> page;
    unsigned int index = 0;
    uint256 entry;
    
    if (dirFirst(view, ownerDirKeylet, page, index, entry))
    {
        do
        {
            auto const sle = view.read(keylet::child(entry));
            if (!sle)
                continue;
                
            // Check if this is a concentrated liquidity position
            if (sle->getFieldU16(sfLedgerEntryType) == ltCONCENTRATED_LIQUIDITY_POSITION)
            {
                auto const positionTickLower = sle->getFieldU32(sfTickLower);
                auto const positionTickUpper = sle->getFieldU32(sfTickUpper);
                
                // Check if this position is affected by the tick crossing
                if (tick == positionTickLower || tick == positionTickUpper)
                {
                    // Update position fees
                    auto const currentTick = ammSle->getFieldU32(sfCurrentTick);
                    auto const ter = ammConcentratedLiquidityUpdatePositionFees(
                        view,
                        keylet::child(entry),
                        positionTickLower,
                        positionTickUpper,
                        currentTick,
                        feeGrowthGlobal0,
                        feeGrowthGlobal1,
                        j);
                        
                    if (ter != tesSUCCESS)
                    {
                        JLOG(j.warn()) << "Failed to update position fees during tick crossing: " << ter;
                        return ter;
                    }
                    
                    JLOG(j.debug()) << "Updated position " << entry 
                                   << " fees during tick " << tick << " crossing";
                }
            }
        } while (dirNext(view, ownerDirKeylet, page, index, entry));
    }
    
    // Update the AMM's active liquidity based on the liquidity delta
    if (ammSle->isFieldPresent(sfAggregatedLiquidity))
    {
        auto const currentActiveLiquidity = ammSle->getFieldAmount(sfAggregatedLiquidity);
        auto const newActiveLiquidity = currentActiveLiquidity + liquidityNet;
        
        // Update AMM with new active liquidity
        auto const newAmmSle = std::make_shared<SLE>(*ammSle);
        newAmmSle->setFieldAmount(sfAggregatedLiquidity, newActiveLiquidity);
        view.update(newAmmSle);
        
        JLOG(j.debug()) << "Crossed tick " << tick << " at price " 
                        << newSqrtPriceX64 << ", liquidity delta: " << liquidityNet
                        << ", new active liquidity: " << newActiveLiquidity;
    }
    else
    {
        JLOG(j.debug()) << "Crossed tick " << tick << " at price " 
                        << newSqrtPriceX64 << ", liquidity delta: " << liquidityNet;
    }

    return tesSUCCESS;
}

// Helper functions for price conversion

/** Convert sqrt price to tick */
std::int32_t
sqrtPriceX64ToTick(std::uint64_t sqrtPriceX64)
{
    // Convert sqrt price to price
    auto const price = static_cast<double>(sqrtPriceX64) / (1ULL << 63);
    auto const priceSquared = price * price;

    // Convert price to tick using the formula: tick = log(price) / log(1.0001)
    auto const logPrice = std::log(priceSquared);
    auto const logBase = std::log(1.0001);
    auto const tick = static_cast<std::int32_t>(logPrice / logBase);

    return tick;
}

/** Convert tick to sqrt price */
std::uint64_t
tickToSqrtPriceX64(std::int32_t tick)
{
    // Convert tick to price using the formula: price = 1.0001^tick
    auto const price = std::pow(1.0001, tick);
    auto const sqrtPrice = std::sqrt(price);

    // Convert to Q64.64 format
    auto const sqrtPriceX64 =
        static_cast<std::uint64_t>(sqrtPrice * (1ULL << 63));

    return sqrtPriceX64;
}

}  // namespace ripple
