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

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/STObject.h>

namespace ripple {

std::pair<STAmount, STAmount>
ammPoolHolds(
    ReadView const& view,
    AccountID const& ammAccountID,
    Asset const& asset1,
    Asset const& asset2,
    FreezeHandling freezeHandling,
    AuthHandling authHandling,
    beast::Journal const j)
{
    auto const assetInBalance = accountHolds(
        view, ammAccountID, asset1, freezeHandling, authHandling, j);
    auto const assetOutBalance = accountHolds(
        view, ammAccountID, asset2, freezeHandling, authHandling, j);
    return std::make_pair(assetInBalance, assetOutBalance);
}

Expected<std::tuple<STAmount, STAmount, STAmount>, TER>
ammHolds(
    ReadView const& view,
    SLE const& ammSle,
    std::optional<Asset> const& optAsset1,
    std::optional<Asset> const& optAsset2,
    FreezeHandling freezeHandling,
    AuthHandling authHandling,
    beast::Journal const j)
{
    auto const assets = [&]() -> std::optional<std::pair<Asset, Asset>> {
        auto const asset1 = ammSle[sfAsset];
        auto const asset2 = ammSle[sfAsset2];
        if (optAsset1 && optAsset2)
        {
            if (invalidAMMAssetPair(
                    *optAsset1,
                    *optAsset2,
                    std::make_optional(std::make_pair(asset1, asset2))))
            {
                // This error can only be hit if the AMM is corrupted
                // LCOV_EXCL_START
                JLOG(j.debug()) << "ammHolds: Invalid optAsset1 or optAsset2 "
                                << *optAsset1 << " " << *optAsset2;
                return std::nullopt;
                // LCOV_EXCL_STOP
            }
            return std::make_optional(std::make_pair(*optAsset1, *optAsset2));
        }
        auto const singleAsset =
            [&asset1, &asset2, &j](
                Asset checkIssue,
                char const* label) -> std::optional<std::pair<Asset, Asset>> {
            if (checkIssue == asset1)
                return std::make_optional(std::make_pair(asset1, asset2));
            else if (checkIssue == asset2)
                return std::make_optional(std::make_pair(asset2, asset1));
            // Unreachable unless AMM corrupted.
            // LCOV_EXCL_START
            JLOG(j.debug())
                << "ammHolds: Invalid " << label << " " << checkIssue;
            return std::nullopt;
            // LCOV_EXCL_STOP
        };
        if (optAsset1)
        {
            return singleAsset(*optAsset1, "optAsset1");
        }
        else if (optAsset2)
        {
            // Cannot have Amount2 without Amount.
            return singleAsset(*optAsset2, "optAsset2");  // LCOV_EXCL_LINE
        }
        return std::make_optional(std::make_pair(asset1, asset2));
    }();
    if (!assets)
        return Unexpected(tecAMM_INVALID_TOKENS);
    auto const [amount1, amount2] = ammPoolHolds(
        view,
        ammSle.getAccountID(sfAccount),
        assets->first,
        assets->second,
        freezeHandling,
        authHandling,
        j);
    return std::make_tuple(amount1, amount2, ammSle[sfLPTokenBalance]);
}

STAmount
ammLPHolds(
    ReadView const& view,
    Asset const& asset1,
    Asset const& asset2,
    AccountID const& ammAccount,
    AccountID const& lpAccount,
    beast::Journal const j)
{
    // This function looks similar to `accountHolds`. However, it only checks if
    // a LPToken holder has enough balance. On the other hand, `accountHolds`
    // checks if the underlying assets of LPToken are frozen with the
    // fixFrozenLPTokenTransfer amendment

    auto const currency = ammLPTCurrency(asset1, asset2);
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

    return view.balanceHookIOU(lpAccount, ammAccount, amount);
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
        ammSle[sfAsset],
        ammSle[sfAsset2],
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
    Asset const& asset)
{
    // Get the actual AMM balance without factoring in the balance hook
    if (asset.holds<MPTIssue>())
    {
        auto const& issue = asset.get<MPTIssue>();
        if (auto const sle = view.read(keylet::mptoken(issue, ammAccountID));
            sle && !isFrozen(view, ammAccountID, issue))
            return STAmount{issue, (*sle)[sfMPTAmount]};
    }
    else
    {
        Issue const& issue = asset.get<Issue>();
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
    }

    return STAmount{asset};
}

static TER
deleteAMMObjects(
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

            if (nodeType == ltMPTOKEN)
            {
                // MPT must have zero balance
                if (sleItem->getFieldU64(sfMPTAmount) != 0)
                {
                    // LCOV_EXCL_START
                    JLOG(j.error()) << "deleteAMMObjects: deleting MPT with "
                                       "non-zero balance.";
                    return {tecINTERNAL, SkipEntry::No};
                    // LCOV_EXCL_STOP
                }

                return {
                    deleteAMMMPToken(sb, sleItem, ammAccountID, j),
                    SkipEntry::No};
            }
            else if (nodeType == LedgerEntryType::ltRIPPLE_STATE)
            {
                // Trustlines must have zero balance
                if (sleItem->getFieldAmount(sfBalance) != beast::zero)
                {
                    // LCOV_EXCL_START
                    JLOG(j.error())
                        << "deleteAMMObjects: deleting trustline with "
                           "non-zero balance.";
                    return {tecINTERNAL, SkipEntry::No};
                    // LCOV_EXCL_STOP
                }

                return {
                    deleteAMMTrustLine(sb, sleItem, ammAccountID, j),
                    SkipEntry::No};
            }
            // LCOV_EXCL_START
            JLOG(j.error())
                << "deleteAMMObjects: deleting non-trustline or non-MPT "
                << nodeType;
            return {tecINTERNAL, SkipEntry::No};
            // LCOV_EXCL_STOP
        },
        j,
        maxTrustlinesToDelete);
}

TER
deleteAMMAccount(
    Sandbox& sb,
    Asset const& asset,
    Asset const& asset2,
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
            deleteAMMObjects(sb, ammAccountID, maxDeletableAMMTrustLines, j);
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
    Asset const& lptAsset,
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
    auctionSlot.setFieldAmount(sfPrice, STAmount{lptAsset, 0});
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
    // AMM account has at most two IOU (pool tokens, not LPToken) trustlines.
    // One or both trustlines could be to the LP if LP is the issuer,
    // or a different account if LP is not an issuer. For instance,
    // if AMM has two tokens USD and EUR and LP is not the issuer of the tokens
    // then the trustlines are between AMM account and the issuer.
    // There is one LPToken trustline for each LP. Only remaining LP has
    // exactly one LPToken trustlines and at most two IOU trustline for each
    // pool token. One or both tokens could be MPT.
    std::uint8_t nIOUTrustLines = 0;
    // There are at most two MPT objects, one for each side of the pool.
    std::uint8_t nMPT = 0;
    // There is only one AMM object
    bool hasAMM = false;
    // AMM LP has at most three trustlines, at most two MPTs, and only one
    // AMM object must exist. If there are more than four objects then
    // it's either an error or there are more than one LP. Ten pages should
    // be sufficient to include four objects.
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
            auto const entryType = sle->getFieldU16(sfLedgerEntryType);
            // Only one AMM object
            if (entryType == ltAMM)
            {
                if (hasAMM)
                    return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
                hasAMM = true;
                continue;
            }
            if (entryType == ltMPTOKEN)
            {
                ++nMPT;
                continue;
            }
            if (entryType != ltRIPPLE_STATE)
                return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
            auto const lowLimit = sle->getFieldAmount(sfLowLimit);
            auto const highLimit = sle->getFieldAmount(sfHighLimit);
            auto const isLPTrustline = lowLimit.getIssuer() == lpAccount ||
                highLimit.getIssuer() == lpAccount;
            auto const isLPTokenTrustline =
                lowLimit.asset() == ammIssue || highLimit.asset() == ammIssue;

            // Liquidity Provider trustline
            if (isLPTrustline)
            {
                // LPToken trustline
                if (isLPTokenTrustline)
                {
                    // LP has exactly one LPToken trustline
                    if (++nLPTokenTrustLines > 1)
                        return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
                }
                // AMM account has at most two IOU trustlines
                else if (++nIOUTrustLines > 2)
                    return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
            }
            // Another Liquidity Provider LPToken trustline
            else if (isLPTokenTrustline)
                return false;
            // AMM account has at most two IOU trustlines
            else if (++nIOUTrustLines > 2)
                return Unexpected<TER>(tecINTERNAL);  // LCOV_EXCL_LINE
        }
        auto const uNodeNext = ownerDir->getFieldU64(sfIndexNext);
        if (uNodeNext == 0)
        {
            if (nLPTokenTrustLines != 1 || (nIOUTrustLines == 0 && nMPT == 0) ||
                (nIOUTrustLines > 2 || nMPT > 2) || (nIOUTrustLines + nMPT) > 2)
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
    if (auto const res =
            isOnlyLiquidityProvider(sb, lpTokens.get<Issue>(), account);
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

}  // namespace ripple
