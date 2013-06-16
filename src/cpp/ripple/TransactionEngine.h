//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef __TRANSACTIONENGINE__
#define __TRANSACTIONENGINE__

DEFINE_INSTANCE (TransactionEngine);

// A TransactionEngine applies serialized transactions to a ledger
// It can also, verify signatures, verify fees, and give rejection reasons

// One instance per ledger.
// Only one transaction applied at a time.
class TransactionEngine : private IS_INSTANCE (TransactionEngine)
{
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
    typedef boost::shared_ptr<TransactionEngine> pointer;

    TransactionEngine () : mTxnSeq (0)
    {
        ;
    }
    TransactionEngine (Ledger::ref ledger) : mLedger (ledger), mTxnSeq (0)
    {
        assert (mLedger);
    }

    LedgerEntrySet& getNodes ()
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

    SLE::pointer        entryCreate (LedgerEntryType type, uint256 const & index)
    {
        return mNodes.entryCreate (type, index);
    }
    SLE::pointer        entryCache (LedgerEntryType type, uint256 const & index)
    {
        return mNodes.entryCache (type, index);
    }
    void                entryDelete (SLE::ref sleEntry)
    {
        mNodes.entryDelete (sleEntry);
    }
    void                entryModify (SLE::ref sleEntry)
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

#endif
// vim:ts=4
