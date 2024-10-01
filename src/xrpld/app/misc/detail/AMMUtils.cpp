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
#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpl/basics/Log.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STObject.h>

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
        auto const issue1 = ammSle[sfAsset];
        auto const issue2 = ammSle[sfAsset2];
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
                const char* label) -> std::optional<std::pair<Issue, Issue>> {
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
    return accountHolds(
        view,
        lpAccount,
        ammLPTCurrency(cur1, cur2),
        ammAccount,
        FreezeHandling::fhZERO_IF_FROZEN,
        j);
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
        ammSle[sfAsset].currency,
        ammSle[sfAsset2].currency,
        ammSle[sfAccount],
        lpAccount,
        j);
}

std::uint16_t
getTradingFee(ReadView const& view, SLE const& ammSle, AccountID const& account)
{
    using namespace std::chrono;
    ASSERT(
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

}  // namespace ripple
