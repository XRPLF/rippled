#ifndef RIPPLE_LEDGERHISTORY_H
#define RIPPLE_LEDGERHISTORY_H

// VFALCO TODO Rename to OldLedgers ?
class LedgerHistory
{
public:
    LedgerHistory ();

    void addLedger (Ledger::pointer ledger);

    void addAcceptedLedger (Ledger::pointer ledger, bool fromConsensus);

    float getCacheHitRate ()
    {
        return mLedgersByHash.getHitRate ();
    }

    Ledger::pointer getLedgerBySeq (LedgerIndex ledgerIndex);

    // VFALCO NOTE shouldn't this call the function above?
    LedgerHash getLedgerHash (LedgerIndex ledgerIndex);

    Ledger::pointer getLedgerByHash (LedgerHash const& ledgerHash);

    Ledger::pointer canonicalizeLedger (Ledger::pointer ledger, bool cache);

    void tune (int size, int age);

    void sweep ()
    {
        mLedgersByHash.sweep ();
    }

private:
    TaggedCache <LedgerHash, Ledger, UptimeTimerAdapter> mLedgersByHash;

    // Maps ledger indexes to the corresponding hash.
    std::map <LedgerIndex, LedgerHash> mLedgersByIndex; // accepted ledgers
};

#endif
