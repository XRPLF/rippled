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
#include <ripple/basics/FeeUnits.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/mulDiv.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
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

    auto const ret = preflight1(ctx);

    if (!isTesSuccess(ret))
        return ret;

    if (ctx.tx[sfAccount] == ctx.tx[sfDestination])
        // An account cannot be deleted and give itself the resulting XRP.
        return temDST_IS_SRC;

    return preflight2(ctx);
}

FeeUnit64
DeleteAccount::calculateBaseFee(ReadView const& view, STTx const& tx)
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

    auto sleDst = ctx.view.read(keylet::account(dst));

    if (!sleDst)
        return tecNO_DST;

    if ((*sleDst)[sfFlags] & lsfRequireDestTag && !ctx.tx[~sfDestinationTag])
        return tecDST_TAG_NEEDED;

    // Check whether the destination account requires deposit authorization.
    if (ctx.view.rules().enabled(featureDepositAuth) &&
        (sleDst->getFlags() & lsfDepositAuth))
    {
        if (!ctx.view.exists(keylet::depositPreauth(dst, account)))
            return tecNO_PERMISSION;
    }

    auto sleAccount = ctx.view.read(keylet::account(account));
    assert(sleAccount);
    if (!sleAccount)
        return terNO_ACCOUNT;

    // We don't allow an account to be deleted if its sequence number
    // is within 256 of the current ledger.  This prevents replay of old
    // transactions if this account is resurrected after it is deleted.
    //
    // We look at the account's Sequence rather than the transaction's
    // Sequence in preparation for Tickets.
    constexpr std::uint32_t seqDelta{255};
    if ((*sleAccount)[sfSequence] + seqDelta > ctx.view.seq())
        return tecTOO_SOON;

    // Verify that the account does not own any objects that would prevent
    // the account from being deleted.
    Keylet const ownerDirKeylet{keylet::ownerDir(account)};
    if (dirIsEmpty(ctx.view, ownerDirKeylet))
        return tesSUCCESS;

    std::shared_ptr<SLE const> sleDirNode{};
    unsigned int uDirEntry{0};
    uint256 dirEntry{beast::zero};

    if (!cdirFirst(
            ctx.view,
            ownerDirKeylet.key,
            sleDirNode,
            uDirEntry,
            dirEntry,
            ctx.j))
        // Account has no directory at all.  This _should_ have been caught
        // by the dirIsEmpty() check earlier, but it's okay to catch it here.
        return tesSUCCESS;

    std::int32_t deletableDirEntryCount{0};
    do
    {
        // Make sure any directory node types that we find are the kind
        // we can delete.
        auto sleItem = ctx.view.read(keylet::child(dirEntry));
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
        ctx.view, ownerDirKeylet.key, sleDirNode, uDirEntry, dirEntry, ctx.j));

    return tesSUCCESS;
}

TER
DeleteAccount::doApply()
{
    auto src = view().peek(keylet::account(account_));
    assert(src);

    auto dst = view().peek(keylet::account(ctx_.tx[sfDestination]));
    assert(dst);

    if (!src || !dst)
        return tefBAD_LEDGER;

    // Delete all of the entries in the account directory.
    Keylet const ownerDirKeylet{keylet::ownerDir(account_)};
    std::shared_ptr<SLE> sleDirNode{};
    unsigned int uDirEntry{0};
    uint256 dirEntry{beast::zero};

    if (view().exists(ownerDirKeylet) &&
        dirFirst(
            view(), ownerDirKeylet.key, sleDirNode, uDirEntry, dirEntry, j_))
    {
        do
        {
            // Choose the right way to delete each directory node.
            auto sleItem = view().peek(keylet::child(dirEntry));
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
            view(), ownerDirKeylet.key, sleDirNode, uDirEntry, dirEntry, j_));
    }

    // Transfer any XRP remaining after the fee is paid to the destination:
    (*dst)[sfBalance] = (*dst)[sfBalance] + mSourceBalance;
    (*src)[sfBalance] = (*src)[sfBalance] - mSourceBalance;
    ctx_.deliver(mSourceBalance);

    assert((*src)[sfBalance] == XRPAmount(0));

    // If there's still an owner directory associated with the source account
    // delete it.
    if (view().exists(ownerDirKeylet) && !view().emptyDirDelete(ownerDirKeylet))
    {
        JLOG(j_.error()) << "DeleteAccount cannot delete root dir node of "
                         << toBase58(account_);
        return tecHAS_OBLIGATIONS;
    }

    // Re-arm the password change fee if we can and need to.
    if (mSourceBalance > XRPAmount(0) && dst->isFlag(lsfPasswordSpent))
        dst->clearFlag(lsfPasswordSpent);

    view().update(dst);
    view().erase(src);

    return tesSUCCESS;
}

}  // namespace ripple
