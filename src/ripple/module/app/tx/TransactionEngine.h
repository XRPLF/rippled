//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef __TRANSACTIONENGINE__
#define __TRANSACTIONENGINE__

namespace ripple {

// A TransactionEngine applies serialized transactions to a ledger
// It can also, verify signatures, verify fees, and give rejection reasons

// One instance per ledger.
// Only one transaction applied at a time.
class TransactionEngine
    : public CountedObject <TransactionEngine>
{
public:
    static char const* getCountedObjectName () { return "TransactionEngine"; }

private:
    LedgerEntrySet      mNodes;

    TER setAuthorized (const SerializedTransaction & txn, bool bMustSetGenerator);
    TER checkSig (const SerializedTransaction & txn);

    TER takeOffers (
        bool                bPassive,
        uint256 const &      uBookBase,
        const uint160 &      uTakerAccountID,
        SLE::ref            sleTakerAccount,
        const STAmount &     saTakerPays,
        const STAmount &     saTakerGets,
        STAmount &           saTakerPaid,
        STAmount &           saTakerGot);

protected:
    Ledger::pointer     mLedger;
    int                 mTxnSeq;

    uint160             mTxnAccountID;
    SLE::pointer        mTxnAccount;

    void                txnWrite ();

public:
    typedef std::shared_ptr<TransactionEngine> pointer;

    TransactionEngine () : mTxnSeq (0)
    {
        ;
    }
    TransactionEngine (Ledger::ref ledger) : mLedger (ledger), mTxnSeq (0)
    {
        assert (mLedger);
    }

    LedgerEntrySet& view ()
    {
        return mNodes;
    }
    Ledger::ref getLedger ()
    {
        return mLedger;
    }
    void setLedger (Ledger::ref ledger)
    {
        assert (ledger);
        mLedger = ledger;
    }

    // VFALCO TODO Remove these pointless wrappers
    SLE::pointer entryCreate (LedgerEntryType type, uint256 const & index)
    {
        return mNodes.entryCreate (type, index);
    }

    SLE::pointer entryCache (LedgerEntryType type, uint256 const & index)
    {
        return mNodes.entryCache (type, index);
    }

    void entryDelete (SLE::ref sleEntry)
    {
        mNodes.entryDelete (sleEntry);
    }

    void entryModify (SLE::ref sleEntry)
    {
        mNodes.entryModify (sleEntry);
    }

    TER applyTransaction (const SerializedTransaction&, TransactionEngineParams, bool & didApply);
    bool checkInvariants (TER result, const SerializedTransaction & txn, TransactionEngineParams params);
};

inline TransactionEngineParams operator| (const TransactionEngineParams& l1, const TransactionEngineParams& l2)
{
    return static_cast<TransactionEngineParams> (static_cast<int> (l1) | static_cast<int> (l2));
}

inline TransactionEngineParams operator& (const TransactionEngineParams& l1, const TransactionEngineParams& l2)
{
    return static_cast<TransactionEngineParams> (static_cast<int> (l1) & static_cast<int> (l2));
}

} // ripple

#endif
