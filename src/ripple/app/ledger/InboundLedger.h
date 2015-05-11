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

#ifndef RIPPLE_APP_LEDGER_INBOUNDLEDGER_H_INCLUDED
#define RIPPLE_APP_LEDGER_INBOUNDLEDGER_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/overlay/PeerSet.h>
#include <ripple/basics/CountedObject.h>
#include <set>

namespace ripple {

// VFALCO TODO Rename to InboundLedger
// A ledger we are trying to acquire
class InboundLedger
    : public PeerSet
    , public std::enable_shared_from_this <InboundLedger>
    , public CountedObject <InboundLedger>
{
public:
    static char const* getCountedObjectName () { return "InboundLedger"; }

    typedef std::shared_ptr <InboundLedger> pointer;
    typedef std::pair < std::weak_ptr<Peer>, std::shared_ptr<protocol::TMLedgerData> > PeerDataPairType;

    // These are the reasons we might acquire a ledger
    enum fcReason
    {
        fcHISTORY,      // Acquiring past ledger
        fcGENERIC,      // Generic other reasons
        fcVALIDATION,   // Validations suggest this ledger is important
        fcCURRENT,      // This might be the current ledger
        fcCONSENSUS,    // We believe the consensus round requires this ledger
    };

public:
    InboundLedger (uint256 const& hash, std::uint32_t seq, fcReason reason, clock_type& clock);

    ~InboundLedger ();

    // Called when another attempt is made to fetch this same ledger
    void update (std::uint32_t seq);

    bool isHeader () const
    {
        return mHaveHeader;
    }
    bool isAcctStComplete () const
    {
        return mHaveState;
    }
    bool isTransComplete () const
    {
        return mHaveTransactions;
    }
    bool isDone () const
    {
        return mAborted || isComplete () || isFailed ();
    }
    Ledger::ref getLedger () const
    {
        return mLedger;
    }
    void abort ()
    {
        mAborted = true;
    }
    std::uint32_t getSeq () const
    {
        return mSeq;
    }

    // VFALCO TODO Make this the Listener / Observer pattern
    bool addOnComplete (std::function<void (InboundLedger::pointer)>);

    void trigger (Peer::ptr const&);
    bool tryLocal ();
    void addPeers ();
    bool checkLocal ();
    void init (ScopedLockType& collectionLock);

    bool gotData (std::weak_ptr<Peer>, std::shared_ptr<protocol::TMLedgerData>);

    typedef std::pair <protocol::TMGetObjectByHash::ObjectType, uint256> neededHash_t;

    std::vector<neededHash_t> getNeededHashes ();

    // VFALCO TODO Replace uint256 with something semanticallyh meaningful
    void filterNodes (std::vector<SHAMapNodeID>& nodeIDs, std::vector<uint256>& nodeHashes,
        int max, bool aggressive);

    /** Return a Json::objectValue. */
    Json::Value getJson (int);
    void runData ();

private:
    void done ();

    void onTimer (bool progress, ScopedLockType& peerSetLock);

    void newPeer (Peer::ptr const& peer)
    {
        // For historical nodes, do not trigger too soon
        // since a fetch pack is probably coming
        if (mReason != fcHISTORY)
            trigger (peer);
    }

    std::weak_ptr <PeerSet> pmDowncast ();

    int processData (std::shared_ptr<Peer> peer, protocol::TMLedgerData& data);

    bool takeHeader (std::string const& data);
    bool takeTxNode (const std::vector<SHAMapNodeID>& IDs, const std::vector<Blob>& data,
                     SHAMapAddNode&);
    bool takeTxRootNode (Blob const& data, SHAMapAddNode&);

    // VFALCO TODO Rename to receiveAccountStateNode
    //             Don't use acronyms, but if we are going to use them at least
    //             capitalize them correctly.
    //
    bool takeAsNode (const std::vector<SHAMapNodeID>& IDs, const std::vector<Blob>& data,
                     SHAMapAddNode&);
    bool takeAsRootNode (Blob const& data, SHAMapAddNode&);

private:
    Ledger::pointer    mLedger;
    bool               mHaveHeader;
    bool               mHaveState;
    bool               mHaveTransactions;
    bool               mAborted;
    bool               mSignaled;
    bool               mByHash;
    std::uint32_t      mSeq;
    fcReason           mReason;

    std::set <uint256> mRecentNodes;

    // Data we have received from peers
    PeerSet::LockType mReceivedDataLock;
    std::vector <PeerDataPairType> mReceivedData;
    bool mReceiveDispatched;

    std::vector <std::function <void (InboundLedger::pointer)> > mOnComplete;
};

} // ripple

#endif
