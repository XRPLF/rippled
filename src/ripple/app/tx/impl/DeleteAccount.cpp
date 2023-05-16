//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#include <ripple/app/tx/impl/DeleteAccount.h>
#include <ripple/app/tx/impl/DepositPreauth.h>
#include <ripple/app/tx/impl/SetSignerList.h>
#include <ripple/app/tx/impl/details/NFTokenUtils.h>
#include <ripple/basics/FeeUnits.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/mulDiv.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/st.h>

namespace ripple {

NotTEC
DeleteAccount::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureDeletableAccounts))
        return temDISABLED;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx[sfAccount] == ctx.tx[sfDestination])
        // An account cannot be deleted and give itself the resulting XRP.
        return temDST_IS_SRC;

    return preflight2(ctx);
}

XRPAmount
DeleteAccount::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // The fee required for AccountDelete is one owner reserve.
    return view.fees().increment;
}

namespace {
// Define a function pointer type that can be used to delete ledger node types.
using DeleterFuncPtr = TER (*)(
    Application& app,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j);

// Local function definitions that provides signature compatibility.
TER
offerDelete(
    Application& app,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j)
{
    return offerDelete(view, sleDel, j);
}

TER
removeSignersFromLedger(
    Application& app,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j)
{
    return SetSignerList::removeFromLedger(app, view, account, j);
}

TER
removeTicketFromLedger(
    Application&,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const&,
    beast::Journal j)
{
    return Transactor::ticketDelete(view, account, delIndex, j);
}

TER
removeDepositPreauthFromLedger(
    Application& app,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j)
{
    return DepositPreauth::removeFromLedger(app, view, delIndex, j);
}

TER
removeNFTokenOfferFromLedger(
    Application& app,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal)
{
    if (!nft::deleteTokenOffer(view, sleDel))
        return tefBAD_LEDGER;

    return tesSUCCESS;
}

// Return nullptr if the LedgerEntryType represents an obligation that can't
// be deleted.  Otherwise return the pointer to the function that can delete
// the non-obligation
DeleterFuncPtr
nonObligationDeleter(LedgerEntryType t)
{
    switch (t)
    {
        case ltOFFER:
            return offerDelete;
        case ltSIGNER_LIST:
            return removeSignersFromLedger;
        case ltTICKET:
            return removeTicketFromLedger;
        case ltDEPOSIT_PREAUTH:
            return removeDepositPreauthFromLedger;
        case ltNFTOKEN_OFFER:
            return removeNFTokenOfferFromLedger;
        default:
            return nullptr;
    }
}

}  // namespace

TER
DeleteAccount::preclaim(PreclaimContext const& ctx)
{
    AccountID const account{ctx.tx[sfAccount]};
    AccountID const dst{ctx.tx[sfDestination]};

    auto const dstAcctRoot = ctx.view.read(keylet::account(dst));

    if (!dstAcctRoot)
        return tecNO_DST;

    if (dstAcctRoot->isFlag(lsfRequireDestTag) && !ctx.tx[~sfDestinationTag])
        return tecDST_TAG_NEEDED;

    // Check whether the destination account requires deposit authorization.
    if (ctx.view.rules().enabled(featureDepositAuth) &&
        dstAcctRoot->isFlag(lsfDepositAuth))
    {
        if (!ctx.view.exists(keylet::depositPreauth(dst, account)))
            return tecNO_PERMISSION;
    }

    auto const srcAcctRoot = ctx.view.read(keylet::account(account));
    assert(srcAcctRoot);
    if (!srcAcctRoot)
        return terNO_ACCOUNT;

    if (ctx.view.rules().enabled(featureNonFungibleTokensV1))
    {
        // If an issuer has any issued NFTs resident in the ledger then it
        // cannot be deleted.
        if (srcAcctRoot->mintedNFTokens() != srcAcctRoot->burnedNFTokens())
            return tecHAS_OBLIGATIONS;

        // If the account owns any NFTs it cannot be deleted.
        Keylet const first = keylet::nftpage_min(account);
        Keylet const last = keylet::nftpage_max(account);

        auto const cp = ctx.view.readSLE(Keylet(
            ltNFTOKEN_PAGE,
            ctx.view.succ(first.key, last.key.next()).value_or(last.key)));
        if (cp)
            return tecHAS_OBLIGATIONS;
    }

    // We don't allow an account to be deleted if its sequence number
    // is within 256 of the current ledger.  This prevents replay of old
    // transactions if this account is resurrected after it is deleted.
    //
    // We look at the account's Sequence rather than the transaction's
    // Sequence in preparation for Tickets.
    constexpr std::uint32_t seqDelta{255};
    if (srcAcctRoot->sequence() + seqDelta > ctx.view.seq())
        return tecTOO_SOON;

    // When fixNFTokenRemint is enabled, we don't allow an account to be
    // deleted if <FirstNFTokenSequence + MintedNFTokens> is within 256 of the
    // current ledger. This is to prevent having duplicate NFTokenIDs after
    // account re-creation.
    //
    // Without this restriction, duplicate NFTokenIDs can be reproduced when
    // authorized minting is involved. Because when the minter mints a NFToken,
    // the issuer's sequence does not change. So when the issuer re-creates
    // their account and mints a NFToken, it is possible that the
    // NFTokenSequence of this NFToken is the same as the one that the
    // authorized minter minted in a previous ledger.
    if (ctx.view.rules().enabled(fixNFTokenRemint) &&
        (srcAcctRoot->firstNFTokenSequence().value_or(0) +
             srcAcctRoot->mintedNFTokens().value_or(0) + seqDelta >
         ctx.view.seq()))
        return tecTOO_SOON;

    // Verify that the account does not own any objects that would prevent
    // the account from being deleted.
    Keylet const ownerDirKeylet{keylet::ownerDir(account)};
    if (dirIsEmpty(ctx.view, ownerDirKeylet))
        return tesSUCCESS;

    std::shared_ptr<SLE const> sleDirNode{};
    unsigned int uDirEntry{0};
    uint256 dirEntry{beast::zero};

    // Account has no directory at all.  This _should_ have been caught
    // by the dirIsEmpty() check earlier, but it's okay to catch it here.
    if (!cdirFirst(
            ctx.view, ownerDirKeylet.key, sleDirNode, uDirEntry, dirEntry))
        return tesSUCCESS;

    std::int32_t deletableDirEntryCount{0};
    do
    {
        // Make sure any directory node types that we find are the kind
        // we can delete.
        auto sleItem = ctx.view.readSLE(keylet::child(dirEntry));
        if (!sleItem)
        {
            // Directory node has an invalid index.  Bail out.
            JLOG(ctx.j.fatal())
                << "DeleteAccount: directory node in ledger " << ctx.view.seq()
                << " has index to object that is missing: "
                << to_string(dirEntry);
            return tefBAD_LEDGER;
        }

        LedgerEntryType const nodeType{
            safe_cast<LedgerEntryType>((*sleItem)[sfLedgerEntryType])};

        if (!nonObligationDeleter(nodeType))
            return tecHAS_OBLIGATIONS;

        // We found a deletable directory entry.  Count it.  If we find too
        // many deletable directory entries then bail out.
        if (++deletableDirEntryCount > maxDeletableDirEntries)
            return tefTOO_BIG;

    } while (cdirNext(
        ctx.view, ownerDirKeylet.key, sleDirNode, uDirEntry, dirEntry));

    return tesSUCCESS;
}

TER
DeleteAccount::doApply()
{
    auto srcAcctRoot = view().peek(keylet::account(account_));
    assert(srcAcctRoot);

    auto dstAcctRoot = view().peek(keylet::account(ctx_.tx[sfDestination]));
    assert(dstAcctRoot);

    if (!srcAcctRoot || !dstAcctRoot)
        return tefBAD_LEDGER;

    // Delete all of the entries in the account directory.
    Keylet const ownerDirKeylet{keylet::ownerDir(account_)};
    std::shared_ptr<SLE> sleDirNode{};
    unsigned int uDirEntry{0};
    uint256 dirEntry{beast::zero};

    if (view().exists(ownerDirKeylet) &&
        dirFirst(view(), ownerDirKeylet.key, sleDirNode, uDirEntry, dirEntry))
    {
        do
        {
            // Choose the right way to delete each directory node.
            auto sleItem = view().peekSLE(keylet::child(dirEntry));
            if (!sleItem)
            {
                // Directory node has an invalid index.  Bail out.
                JLOG(j_.fatal())
                    << "DeleteAccount: Directory node in ledger "
                    << view().seq() << " has index to object that is missing: "
                    << to_string(dirEntry);
                return tefBAD_LEDGER;
            }

            LedgerEntryType const nodeType{safe_cast<LedgerEntryType>(
                sleItem->getFieldU16(sfLedgerEntryType))};

            if (auto deleter = nonObligationDeleter(nodeType))
            {
                TER const result{
                    deleter(ctx_.app, view(), account_, dirEntry, sleItem, j_)};

                if (!isTesSuccess(result))
                    return result;
            }
            else
            {
                assert(!"Undeletable entry should be found in preclaim.");
                JLOG(j_.error())
                    << "DeleteAccount undeletable item not found in preclaim.";
                return tecHAS_OBLIGATIONS;
            }

            // dirFirst() and dirNext() are like iterators with exposed
            // internal state.  We'll take advantage of that exposed state
            // to solve a common C++ problem: iterator invalidation while
            // deleting elements from a container.
            //
            // We have just deleted one directory entry, which means our
            // "iterator state" is invalid.
            //
            //  1. During the process of getting an entry from the
            //     directory uDirEntry was incremented from 0 to 1.
            //
            //  2. We then deleted the entry at index 0, which means the
            //     entry that was at 1 has now moved to 0.
            //
            //  3. So we verify that uDirEntry is indeed 1.  Then we jam it
            //     back to zero to "un-invalidate" the iterator.
            assert(uDirEntry == 1);
            if (uDirEntry != 1)
            {
                JLOG(j_.error())
                    << "DeleteAccount iterator re-validation failed.";
                return tefBAD_LEDGER;
            }
            uDirEntry = 0;

        } while (dirNext(
            view(), ownerDirKeylet.key, sleDirNode, uDirEntry, dirEntry));
    }

    // Transfer any XRP remaining after the fee is paid to the destination:
    dstAcctRoot->setBalance(dstAcctRoot->balance() + mSourceBalance);
    srcAcctRoot->setBalance(srcAcctRoot->balance() - mSourceBalance);
    ctx_.deliver(mSourceBalance);

    assert(srcAcctRoot->balance() == XRPAmount(0));

    // If there's still an owner directory associated with the source account
    // delete it.
    if (view().exists(ownerDirKeylet) && !view().emptyDirDelete(ownerDirKeylet))
    {
        JLOG(j_.error()) << "DeleteAccount cannot delete root dir node of "
                         << toBase58(account_);
        return tecHAS_OBLIGATIONS;
    }

    // Re-arm the password change fee if we can and need to.
    if (mSourceBalance > XRPAmount(0) && dstAcctRoot->isFlag(lsfPasswordSpent))
        dstAcctRoot->clearFlag(lsfPasswordSpent);

    view().update(dstAcctRoot);
    view().erase(srcAcctRoot);

    return tesSUCCESS;
}

}  // namespace ripple
