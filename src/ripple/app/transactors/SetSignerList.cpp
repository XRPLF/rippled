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

#include <ripple/app/transactors/impl/SignerEntries.h>
#include <cstdint>
#include <algorithm>

namespace ripple {

/**
The ttSIGNER_LIST_SET transaction hits bottom right here.
<p>
Let's start with a little background.  Your typical account has a single owner.
Transactions on that account require a single signature from that account
owner.  In the larger world accounts can be more complicated with, say,
multiple owners or multiple signers.  That's where SignerLists come into play.
<p>
Any account can have one SignerList attached to it.  A SignerList contains the
following elements:
<p><ul>
<li> A list of 2 to 32 signers.  Each signer in the array consists of:
<ul>
<li> The signer's 160-bit account ID and
<li> The signer's 16-bit weight (used to calculate whether a quorum is met).
</ul>
<li> And, for the entire list, a single 32-bit quorum value.
</ul><p>
Giving the signers different weights allows an account to organize signers so
some are more important than others.  A signer with a larger weight has more
significance in a vote.
<p>
A multi-signed transaction is validated like this:
<p><ul>
<li> Each signer of the transaction  has their signature validated.
<li> The weights of all valid signers are summed.
<li> If the sum of the weights equals or exceeds the quorum value then the
entire transaction is considered signed.  If the sum is below the quorum,
then the signature fails with a tefBAD_QUORUM.
</ul><p>
By making the signer weights 16 bits and the quorum value 32 bits we can avoid
concerns about overflows and still have plenty of resolution.
<p>
This transactor allows two operations:
<ul>
<li> Create (or replace) a signer list for the target account.
<li> Remove any signer list from the target account.
</ul>
The data for a transaction creating or replacing a signer list has this
general form:
<pre>
<code>
{
    "TransactionType": "SignerListSet",
    "Account": "rDg53Haik2475DJx8bjMDSDPj4VX7htaMd",
    "SignerQuorum": 7,
    "SignerEntries": [
        {
            "SignerEntry": {
                "Account": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                "SignerWeight": 4
            }
        },
        {
            "SignerEntry": {
                "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                "SignerWeight": 3
            }
        }
    ]
}
<\code>
</pre>
<p>
The data for a transaction that removes any signer list has this form:
<pre>
<code>
{
    "TransactionType": "SignerListSet",
    "Account": "rDg53Haik2475DJx8bjMDSDPj4VX7htaMd",
    "SignerQuorum": 0
}
<\code>
<\pre>
*/
class SetSignerList final
    : public Transactor
{
public:
    SetSignerList (
        SerializedTransaction const& txn,
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

private:
    // Handlers for supported requests
    TER replaceSignerList (std::uint32_t quorum, uint256 const& index);
    TER destroySignerList (uint256 const& index);

    using SignerEntryVec = SignerEntries::SignerEntryVec;

    // signers are not const because method (intentionally) sorts vector.
    TER validateQuorumAndSignerEntries (
        std::uint32_t quorum, SignerEntryVec& signers) const;

    void writeSignersToLedger (
        SLE::pointer ledgerEntry,
        std::uint32_t quorum,
        SignerEntryVec const& signers);
};

//------------------------------------------------------------------------------

TER SetSignerList::doApply ()
{
    assert (mTxnAccount);

    // All operations require our ledger index.  Compute that once and pass it
    // to our handlers.
    uint256 const index = Ledger::getSignerListIndex (mTxnAccountID);

    // Check the quorum.  A non-zero quorum means we're creating or replacing
    // the list.  A zero quorum means we're destroying the list.
    std::uint32_t const quorum (mTxn.getFieldU32 (sfSignerQuorum));

    bool const hasSignerEntries (mTxn.isFieldPresent (sfSignerEntries));
    if (quorum && hasSignerEntries)
        return replaceSignerList (quorum, index);

    if ((quorum == 0) && !hasSignerEntries)
        return destroySignerList (index);

    if (m_journal.trace) m_journal.trace <<
        "Malformed transaction: Invalid signer set list format.";
    return temMALFORMED;
}

TER
SetSignerList::replaceSignerList (std::uint32_t quorum, uint256 const& index)
{
    if (!mTxn.isFieldPresent (sfSignerEntries))
    {
        if (m_journal.trace) m_journal.trace <<
            "Malformed transaction: Need signer entry array.";
        return temMALFORMED;
    }

    SignerEntries::SignerEntriesDecode signers (
        SignerEntries::deserializeSignerEntries (
            mTxn, m_journal, "transaction"));

    if (signers.ter != tesSUCCESS)
        return signers.ter;

    // Validate our settings.
    if (TER const ter = validateQuorumAndSignerEntries (quorum, signers.vec))
        return ter;

    // This may be either a create or a replace.  Preemptively destroy any
    // old signer list.
    if (TER const ter = destroySignerList (index))
        return ter;

    // Everything's ducky.  Add the ltSIGNER_LIST to the ledger.
    SLE::pointer signerList (mEngine->entryCreate (ltSIGNER_LIST, index));
    writeSignersToLedger (signerList, quorum, signers.vec);

    // Lambda for call to dirAdd.
    auto describer = [&] (SLE::ref sle, bool dummy)
        {
            Ledger::ownerDirDescriber (sle, dummy, mTxnAccountID);
        };

    // Add the signer list to the account's directory.
    std::uint64_t hint;
    TER result = mEngine->view ().dirAdd (
        hint, Ledger::getOwnerDirIndex (mTxnAccountID), index, describer);

    if (m_journal.trace) m_journal.trace <<
        "Creating signer list for account " <<
        mTxnAccountID << ": " << transHuman (result);

    if (result != tesSUCCESS)
        return result;

    signerList->setFieldU64 (sfOwnerNode, hint);

    // If we succeeded, the new entry counts against the creator's reserve.
    mEngine->view ().incrementOwnerCount (mTxnAccount);

    return result;
}

TER SetSignerList::destroySignerList (uint256 const& index)
{
    // See if there's an ltSIGNER_LIST for this account.
    SLE::pointer signerList =
        mEngine->view ().entryCache (ltSIGNER_LIST, index);

    // If the signer list doesn't exist we've already succeeded in deleting it.
    if (!signerList)
        return tesSUCCESS;

    // Remove the node from the account directory.
    std::uint64_t const hint (signerList->getFieldU64 (sfOwnerNode));

    TER const result  = mEngine->view ().dirDelete (false, hint,
        Ledger::getOwnerDirIndex (mTxnAccountID), index, false, (hint == 0));

    if (result == tesSUCCESS)
        mEngine->view ().decrementOwnerCount (mTxnAccount);

    mEngine->view ().entryDelete (signerList);

    return result;
}

TER SetSignerList::validateQuorumAndSignerEntries (
    std::uint32_t quorum, SignerEntryVec& signers) const
{
    // Reject if there are too many or too few entries in the list.
    {
        std::size_t const signerCount = signers.size ();
        if ((signerCount < SignerEntries::minSignerEntries)
            || (signerCount > SignerEntries::maxSignerEntries))
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
    // the quorum can be reached.
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

        // Don't verify that the signer accounts exist.  Verifying them is
        // expensive and they may not exist yet due to network phenomena.
    }
    if ((quorum <= 0) || (allSignersWeight < quorum))
    {
        if (m_journal.trace) m_journal.trace <<
            "Quorum is unreachable";
        return temBAD_QUORUM;
    }
    return tesSUCCESS;
}

void SetSignerList::writeSignersToLedger (
    SLE::pointer ledgerEntry,
    std::uint32_t quorum,
    SignerEntryVec const& signers)
{
    // Assign the quorum.
    ledgerEntry->setFieldU32 (sfSignerQuorum, quorum);

    // Create the SignerListArray one STObject at a time.
    STArray::vector list (signers.size ());
    for (auto const& entry : signers)
    {
        boost::ptr_vector <SerializedType> data;
        data.reserve (2);

        auto account = std::make_unique <STAccount> (sfAccount);
        account->setValueH160 (entry.account);
        data.push_back (account.release ());

        auto weight (
            std::make_unique <STUInt16> (sfSignerWeight, entry.weight));
        data.push_back (weight.release ());

        auto signerEntry (std::make_unique <STObject> (sfSignerEntry, data));

        list.push_back (signerEntry.release ());
    }

    // Assign the SignerEntries.
    STArray toLedger(sfSignerEntries, list);
    ledgerEntry->setFieldArray (sfSignerEntries, toLedger);
}

TER
transact_SetSignerList (
    SerializedTransaction const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
    return SetSignerList (txn, params, engine).apply ();
}

} // namespace ripple
