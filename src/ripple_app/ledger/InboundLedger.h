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

#ifndef RIPPLE_INBOUNDLEDGER_H
#define RIPPLE_INBOUNDLEDGER_H

// VFALCO TODO Rename to InboundLedger
// A ledger we are trying to acquire
class InboundLedger
    : public PeerSet
    , public boost::enable_shared_from_this <InboundLedger>
    , public CountedObject <InboundLedger>
{
public:
    static char const* getCountedObjectName () { return "InboundLedger"; }

    typedef boost::shared_ptr <InboundLedger> pointer;
    typedef std::pair < boost::weak_ptr<Peer>, boost::shared_ptr<protocol::TMLedgerData> > PeerDataPairType;

public:
    InboundLedger (uint256 const& hash, uint32 seq);

    virtual ~InboundLedger ();

    bool isBase () const
    {
        return mHaveBase;
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
    Ledger::ref getLedger ()
    {
        return mLedger;
    }
    void abort ()
    {
        mAborted = true;
    }
    uint32 getSeq ()
    {
        return mSeq;
    }

    // VFALCO TODO Make this the Listener / Observer pattern
    bool addOnComplete (FUNCTION_TYPE<void (InboundLedger::pointer)>);

    void trigger (Peer::ref);
    bool tryLocal ();
    void addPeers ();
    bool checkLocal ();
    void init(ScopedLockType& collectionLock, bool couldBeNew);

    bool gotData (boost::weak_ptr<Peer>, boost::shared_ptr<protocol::TMLedgerData>);

    typedef std::pair <protocol::TMGetObjectByHash::ObjectType, uint256> neededHash_t;

    std::vector<neededHash_t> getNeededHashes ();

    static void filterNodes (std::vector<SHAMapNode>& nodeIDs, std::vector<uint256>& nodeHashes,
                             std::set<SHAMapNode>& recentNodes, int max, bool aggressive);

    Json::Value getJson (int);
    void runData ();

private:
    void done ();

    void onTimer (bool progress, ScopedLockType& peerSetLock);

    void newPeer (Peer::ref peer)
    {
        trigger (peer);
    }

    boost::weak_ptr <PeerSet> pmDowncast ();

    int processData (boost::shared_ptr<Peer> peer, protocol::TMLedgerData& data);

    bool takeBase (const std::string& data);
    bool takeTxNode (const std::list<SHAMapNode>& IDs, const std::list<Blob >& data,
                     SHAMapAddNode&);
    bool takeTxRootNode (Blob const& data, SHAMapAddNode&);
    bool takeAsNode (const std::list<SHAMapNode>& IDs, const std::list<Blob >& data,
                     SHAMapAddNode&);
    bool takeAsRootNode (Blob const& data, SHAMapAddNode&);

private:
    Ledger::pointer    mLedger;
    bool               mHaveBase;
    bool               mHaveState;
    bool               mHaveTransactions;
    bool               mAborted;
    bool               mSignaled;
    bool               mByHash;
    uint32             mSeq;

    std::set <SHAMapNode> mRecentTXNodes;
    std::set <SHAMapNode> mRecentASNodes;


    // Data we have received from peers
    PeerSet::LockType mReceivedDataLock;    
    std::vector <PeerDataPairType> mReceivedData;
    bool mReceiveDispatched;

    std::vector <FUNCTION_TYPE <void (InboundLedger::pointer)> > mOnComplete;
};

#endif

// vim:ts=4
