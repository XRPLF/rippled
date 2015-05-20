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
#include <ripple/app/transactors/Transactor.h>
#include <ripple/app/transactors/impl/SignerEntries.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/basics/Log.h>
#include <cstdint>
#include <algorithm>

namespace ripple {

/**
See the README.md for an overview of the SetSignerList transaction that
this class implements.
*/
class SetSignerList final
    : public Transactor
{
private:
    // Values determined during preCheck for use later.
    enum Operation {unknown, set, destroy};
    Operation do_ {unknown};
    std::uint32_t quorum_ {0};
    std::vector<SignerEntries::SignerEntry> signers_;

public:
    SetSignerList (
        STTx const& txn,
        TransactionEngineParams params,
        TransactionEngine* engine)
        : Transactor (
            txn,
            params,
            engine,
            deprecatedLogs().journal("SetSignerList"))
    {

    }

    /**
    Applies the transaction if it is well formed and the ledger state permits.
    */
    TER doApply () override;

protected:
    /**
    Check anything that can be checked without the ledger.
    */
    TER preCheck () override;

private:
    // signers are not const because method (intentionally) sorts vector.
    TER validateQuorumAndSignerEntries (
        std::uint32_t quorum,
        std::vector<SignerEntries::SignerEntry>& signers) const;

    // Methods called by doApply()
    TER replaceSignerList (uint256 const& index);
    TER destroySignerList (uint256 const& index);

    void writeSignersToLedger (SLE::pointer ledgerEntry);

    static std::size_t ownerCountDelta (std::size_t entryCount);
};

//------------------------------------------------------------------------------

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
    // We need the account ID later, so do this check first.
    preCheckAccount ();

    // Check the quorum.  A non-zero quorum means we're creating or replacing
    // the list.  A zero quorum means we're destroying the list.
    quorum_ = (mTxn.getFieldU32 (sfSignerQuorum));

    bool const hasSignerEntries (mTxn.isFieldPresent (sfSignerEntries));
    if (quorum_ && hasSignerEntries)
    {
        SignerEntries::Decoded signers (
            SignerEntries::deserialize (mTxn, m_journal, "transaction"));

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
        if (m_journal.trace) m_journal.trace <<
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
            if (m_journal.trace) m_journal.trace <<
                "Too many or too few signers in signer list.";
            return temMALFORMED;
        }
    }

    // Make sure there are no duplicate signers.
    std::sort (signers.begin (), signers.end ());
    if (std::adjacent_find (
        signers.begin (), signers.end ()) != signers.end ())
    {
        if (m_journal.trace) m_journal.trace <<
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
            if (m_journal.trace) m_journal.trace <<
                "A signer may not self reference account.";
            return temBAD_SIGNER;
        }

        // Don't verify that the signer accounts exist.  Non-existent accounts
        // may be phantom accounts (which are permitted).
    }
    if ((quorum <= 0) || (allSignersWeight < quorum))
    {
        if (m_journal.trace) m_journal.trace <<
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
    std::size_t const oldOwnerCount = mTxnAccount->getFieldU32 (sfOwnerCount);
    std::size_t const addedOwnerCount = ownerCountDelta (signers_.size ());

    std::uint64_t const newReserve =
        mEngine->getLedger ()->getReserve (oldOwnerCount + addedOwnerCount);

    // We check the reserve against the starting balance because we want to
    // allow dipping into the reserve to pay fees.  This behavior is consistent
    // with CreateTicket.
    if (mPriorBalance < newReserve)
        return tecINSUFFICIENT_RESERVE;

    // Everything's ducky.  Add the ltSIGNER_LIST to the ledger.
    SLE::pointer signerList (
        mEngine->view().entryCreate (ltSIGNER_LIST, index));
    writeSignersToLedger (signerList);

    // Lambda for call to dirAdd.
    auto describer = [&] (SLE::ref sle, bool dummy)
        {
            Ledger::ownerDirDescriber (sle, dummy, mTxnAccountID);
        };

    // Add the signer list to the account's directory.
    std::uint64_t hint;
    TER result = mEngine->view ().dirAdd (
        hint, getOwnerDirIndex (mTxnAccountID), index, describer);

    if (m_journal.trace) m_journal.trace <<
        "Create signer list for account " <<
        mTxnAccountID << ": " << transHuman (result);

    if (result != tesSUCCESS)
        return result;

    signerList->setFieldU64 (sfOwnerNode, hint);

    // If we succeeded, the new entry counts against the creator's reserve.
    mEngine->view ().increaseOwnerCount (mTxnAccount, addedOwnerCount);

    return result;
}

TER
SetSignerList::destroySignerList (uint256 const& index)
{
    // See if there's an ltSIGNER_LIST for this account.
    SLE::pointer signerList =
        mEngine->view ().entryCache (ltSIGNER_LIST, index);

    // If the signer list doesn't exist we've already succeeded in deleting it.
    if (!signerList)
        return tesSUCCESS;

    // We have to examine the current SignerList so we know how much to
    // reduce the OwnerCount.
    std::size_t removeFromOwnerCount = 0;
    uint256 const signerListIndex = getSignerListIndex (mTxnAccountID);
    SLE::pointer accountSignersList =
        mEngine->view ().entryCache (ltSIGNER_LIST, signerListIndex);
    if (accountSignersList)
    {
        STArray const& actualList =
            accountSignersList->getFieldArray (sfSignerEntries);
        removeFromOwnerCount = ownerCountDelta (actualList.size ());
    }

    // Remove the node from the account directory.
    std::uint64_t const hint (signerList->getFieldU64 (sfOwnerNode));

    TER const result  = mEngine->view ().dirDelete (false, hint,
        getOwnerDirIndex (mTxnAccountID), index, false, (hint == 0));

    if (result == tesSUCCESS)
        mEngine->view ().decreaseOwnerCount (mTxnAccount, removeFromOwnerCount);

    mEngine->view ().entryDelete (signerList);

    return result;
}

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
        obj.setFieldAccount (sfAccount, entry.account);
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

TER
transact_SetSignerList (
    STTx const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
    return SetSignerList (txn, params, engine).apply ();
}

} // namespace ripple
