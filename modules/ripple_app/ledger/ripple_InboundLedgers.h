//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_INBOUNDLEDGERS_H
#define RIPPLE_INBOUNDLEDGERS_H

/** Manages the lifetime of inbound ledgers.

    @see InboundLedger
*/
// VFALCO TODO Rename to InboundLedgers
// VFALCO TODO Create abstract interface
class InboundLedgers : LeakChecked <InboundLedger>
{
public:
    // How long before we try again to acquire the same ledger
    static const int kReacquireIntervalSeconds = 600;

    InboundLedgers ()
        : mRecentFailures ("LedgerAcquireRecentFailures", 0, kReacquireIntervalSeconds)
    {
    }

    // VFALCO TODO Should this be called findOrAdd ?
    //
    InboundLedger::pointer findCreate (uint256 const& hash, uint32 seq);

    InboundLedger::pointer find (uint256 const& hash);

    bool hasLedger (LedgerHash const& ledgerHash);

    void dropLedger (LedgerHash const& ledgerHash);

    bool awaitLedgerData (LedgerHash const& ledgerHash);

    // VFALCO TODO Why is hash passed by value?
    // VFALCO TODO Remove the dependency on the Peer object.
    //
    void gotLedgerData (Job&,
                        LedgerHash hash,
                        boost::shared_ptr <protocol::TMLedgerData> packet,
                        boost::weak_ptr<Peer> peer);

    int getFetchCount (int& timeoutCount);

    void logFailure (uint256 const& h)
    {
        mRecentFailures.add (h);
    }

    bool isFailure (uint256 const& h)
    {
        return mRecentFailures.isPresent (h, false);
    }

    void clearFailures();

    Json::Value getInfo();

    void gotFetchPack (Job&);
    void sweep ();

private:
    boost::mutex mLock;
    std::map <uint256, InboundLedger::pointer> mLedgers;
    KeyCache <uint256, UptimeTimerAdapter> mRecentFailures;
};

#endif
