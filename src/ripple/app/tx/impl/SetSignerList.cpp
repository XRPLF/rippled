//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/tx/impl/SetSignerList.h>
#include <ripple/app/tx/impl/SignerEntries.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/basics/Log.h>
#include <cstdint>
#include <algorithm>

namespace ripple {

TER
SetSignerList::doApply ()
{
    assert (mTxnAccount);

    // All operations require our ledger index.  Compute that once and pass it
    // to our handlers.
    uint256 const index = getSignerListIndex (mTxnAccountID);

    // Perform the operation preCheck() decided on.
    switch (do_)
    {
    case set:
        return replaceSignerList (index);

    case destroy:
        return destroySignerList (index);

    default:
        // Fall through intentionally
        break;
    }
    assert (false); // Should not be possible to get here.
    return temMALFORMED;
}

TER
SetSignerList::preCheck()
{
#if ! RIPPLE_ENABLE_MULTI_SIGN
    if (! (view().flags() & tapENABLE_TESTING))
        return temDISABLED;
#endif

    // We need the account ID later, so do this check first.
    preCheckAccount ();

    // Check the quorum.  A non-zero quorum means we're creating or replacing
    // the list.  A zero quorum means we're destroying the list.
    quorum_ = (mTxn.getFieldU32 (sfSignerQuorum));

    bool const hasSignerEntries (mTxn.isFieldPresent (sfSignerEntries));
    if (quorum_ && hasSignerEntries)
    {
        SignerEntries::Decoded signers (
            SignerEntries::deserialize (mTxn, j_, "transaction"));

        if (signers.ter != tesSUCCESS)
            return signers.ter;

        // Validate our settings.
        if (TER const ter =
            validateQuorumAndSignerEntries (quorum_, signers.vec))
        {
            return ter;
        }

        // Save deserialized and validated list for later.
        signers_ = std::move (signers.vec);
        do_ = set;
    }
    else if ((quorum_ == 0) && !hasSignerEntries)
    {
        do_ = destroy;
    }
    else
    {
        // Neither a set nor a destroy.  Malformed.
        if (j_.trace) j_.trace <<
            "Malformed transaction: Invalid signer set list format.";
        return temMALFORMED;
    }

    return preCheckSigningKey ();
}

TER
SetSignerList::validateQuorumAndSignerEntries (
    std::uint32_t quorum,
    std::vector<SignerEntries::SignerEntry>& signers) const
{
    // Reject if there are too many or too few entries in the list.
    {
        std::size_t const signerCount = signers.size ();
        if ((signerCount < SignerEntries::minEntries)
            || (signerCount > SignerEntries::maxEntries))
        {
            if (j_.trace) j_.trace <<
                "Too many or too few signers in signer list.";
            return temMALFORMED;
        }
    }

    // Make sure there are no duplicate signers.
    std::sort (signers.begin (), signers.end ());
    if (std::adjacent_find (
        signers.begin (), signers.end ()) != signers.end ())
    {
        if (j_.trace) j_.trace <<
            "Duplicate signers in signer list";
        return temBAD_SIGNER;
    }

    // Make sure no signers reference this account.  Also make sure the
    // quorum can be reached.
    std::uint64_t allSignersWeight (0);
    for (auto const& signer : signers)
    {
        allSignersWeight += signer.weight;

        if (signer.account == mTxnAccountID)
        {
            if (j_.trace) j_.trace <<
                "A signer may not self reference account.";
            return temBAD_SIGNER;
        }

        // Don't verify that the signer accounts exist.  Non-existent accounts
        // may be phantom accounts (which are permitted).
    }
    if ((quorum <= 0) || (allSignersWeight < quorum))
    {
        if (j_.trace) j_.trace <<
            "Quorum is unreachable";
        return temBAD_QUORUM;
    }
    return tesSUCCESS;
}

TER
SetSignerList::replaceSignerList (uint256 const& index)
{
    // This may be either a create or a replace.  Preemptively destroy any
    // old signer list.  May reduce the reserve, so this is done before
    // checking the reserve.
    if (TER const ter = destroySignerList (index))
        return ter;

    // Compute new reserve.  Verify the account has funds to meet the reserve.
    std::uint32_t const oldOwnerCount = mTxnAccount->getFieldU32 (sfOwnerCount);
    std::uint32_t const addedOwnerCount = ownerCountDelta (signers_.size ());

    auto const newReserve =
        view().fees().accountReserve(
            oldOwnerCount + addedOwnerCount);

    // We check the reserve against the starting balance because we want to
    // allow dipping into the reserve to pay fees.  This behavior is consistent
    // with CreateTicket.
    if (mPriorBalance < newReserve)
        return tecINSUFFICIENT_RESERVE;

    // Everything's ducky.  Add the ltSIGNER_LIST to the ledger.
    auto signerList = std::make_shared<SLE>(ltSIGNER_LIST, index);
    view().insert (signerList);
    writeSignersToLedger (signerList);

    // Lambda for call to dirAdd.
    auto describer = [&] (SLE::ref sle, bool dummy)
        {
            ownerDirDescriber (sle, dummy, mTxnAccountID);
        };

    // Add the signer list to the account's directory.
    std::uint64_t hint;
    TER result = dirAdd(ctx_.view (),
        hint, getOwnerDirIndex (mTxnAccountID), index, describer);

    if (j_.trace) j_.trace <<
        "Create signer list for account " <<
        mTxnAccountID << ": " << transHuman (result);

    if (result != tesSUCCESS)
        return result;

    signerList->setFieldU64 (sfOwnerNode, hint);

    // If we succeeded, the new entry counts against the creator's reserve.
    adjustOwnerCount(view(),
        mTxnAccount, addedOwnerCount);

    return result;
}

TER
SetSignerList::destroySignerList (uint256 const& index)
{
    // See if there's an ltSIGNER_LIST for this account.
    SLE::pointer signerList =
        view().peek (keylet::signers(index));

    // If the signer list doesn't exist we've already succeeded in deleting it.
    if (!signerList)
        return tesSUCCESS;

    // We have to examine the current SignerList so we know how much to
    // reduce the OwnerCount.
    std::uint32_t removeFromOwnerCount = 0;
    auto const k = keylet::signers(mTxnAccountID);
    SLE::pointer accountSignersList =
        view().peek (k);
    if (accountSignersList)
    {
        STArray const& actualList =
            accountSignersList->getFieldArray (sfSignerEntries);
        removeFromOwnerCount = ownerCountDelta (actualList.size ());
    }

    // Remove the node from the account directory.
    std::uint64_t const hint (signerList->getFieldU64 (sfOwnerNode));

    TER const result  = dirDelete(ctx_.view (), false, hint,
        getOwnerDirIndex (mTxnAccountID), index, false, (hint == 0));

    if (result == tesSUCCESS)
        adjustOwnerCount(view(),
            mTxnAccount, removeFromOwnerCount);

    ctx_.view ().erase (signerList);

    return result;
}

// VFALCO NOTE This name is misleading, the signers
//             are not written to the ledger they are
//             added to the SLE.
void
SetSignerList::writeSignersToLedger (SLE::pointer ledgerEntry)
{
    // Assign the quorum.
    ledgerEntry->setFieldU32 (sfSignerQuorum, quorum_);

    // Create the SignerListArray one SignerEntry at a time.
    STArray toLedger (signers_.size ());
    for (auto const& entry : signers_)
    {
        toLedger.emplace_back(sfSignerEntry);
        STObject& obj = toLedger.back();
        obj.reserve (2);
        obj.setAccountID (sfAccount, entry.account);
        obj.setFieldU16 (sfSignerWeight, entry.weight);
    }

    // Assign the SignerEntries.
    ledgerEntry->setFieldArray (sfSignerEntries, toLedger);
}

std::size_t
SetSignerList::ownerCountDelta (std::size_t entryCount)
{
    // We always compute the full change in OwnerCount, taking into account:
    //  o The fact that we're adding/removing a SignerList and
    //  o Accounting for the number of entries in the list.
    // We can get away with that because lists are not adjusted incrementally;
    // we add or remove an entire list.

    // The wiki (https://wiki.ripple.com/Multisign#Fees_2) currently says
    // (December 2014) the reserve should be
    //   Reserve * (N + 1) / 2
    // That's not making sense to me right now, since I'm working in
    // integral OwnerCount units.  If, say, N is 4 I don't know how to return
    // 4.5 units as an integer.
    //
    // So, just to get started, I'm saying that:
    //  o Simply having a SignerList costs 2 OwnerCount units.
    //  o And each signer in the list costs 1 more OwnerCount unit.
    // So, at a minimum, adding a SignerList with 2 entries costs 4 OwnerCount
    // units.  A SignerList with 8 entries would cost 10 OwnerCount units.
    //
    // It's worth noting that once this reserve policy has gotten into the
    // wild it will be very difficult to change.  So think hard about what
    // we want for the long term.
    return 2 + entryCount;
}

} // namespace ripple
