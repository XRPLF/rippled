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

#ifndef RIPPLE_INBOUNDLEDGERS_H
#define RIPPLE_INBOUNDLEDGERS_H

/** Manages the lifetime of inbound ledgers.

    @see InboundLedger
*/
// VFALCO TODO Rename to InboundLedgers
// VFALCO TODO Create abstract interface
class InboundLedgers
    : public Stoppable
    , public LeakChecked <InboundLedger>
{
public:
    // How long before we try again to acquire the same ledger
    static const int kReacquireIntervalSeconds = 300;

    explicit InboundLedgers (Stoppable& parent);

    // VFALCO TODO Should this be called findOrAdd ?
    //
    InboundLedger::pointer findCreate (uint256 const& hash, uint32 seq, bool bCouldBeNew);

    InboundLedger::pointer find (uint256 const& hash);

    InboundLedger::pointer findCreateConsensusLedger (uint256 const& hash);
    InboundLedger::pointer findCreateValidationLedger (uint256 const& hash);

    bool hasLedger (LedgerHash const& ledgerHash);

    void dropLedger (LedgerHash const& ledgerHash);

    bool gotLedgerData (LedgerHash const& ledgerHash, boost::shared_ptr<Peer>, boost::shared_ptr <protocol::TMLedgerData>);

    void doLedgerData (Job&, LedgerHash hash);

    void gotStaleData (boost::shared_ptr <protocol::TMLedgerData> packet);

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

    void onStop ();

private:
    typedef boost::unordered_map <uint256, InboundLedger::pointer> MapType;

    typedef RippleRecursiveMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;
    LockType mLock;

    MapType mLedgers;
    KeyCache <uint256, UptimeTimerAdapter> mRecentFailures;

    uint256 mConsensusLedger;
    uint256 mValidationLedger;
};

#endif
