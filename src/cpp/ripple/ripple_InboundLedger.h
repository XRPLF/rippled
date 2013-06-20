//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
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
    typedef boost::shared_ptr <InboundLedger> pointer;

public:
    InboundLedger (uint256 const& hash, uint32 seq);

    virtual ~InboundLedger ()
    {
        ;
    }

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

    // VFALCO NOTE what is the meaning of the return value?
    bool setAccept ()
    {
        if (mAccept)
            return false;

        mAccept = true;

        return true;
    }

    // VFALCO TODO Make thise the Listener / Observer pattern
    bool addOnComplete (FUNCTION_TYPE<void (InboundLedger::pointer)>);

    bool takeBase (const std::string& data);
    bool takeTxNode (const std::list<SHAMapNode>& IDs, const std::list<Blob >& data,
                     SHAMapAddNode&);
    bool takeTxRootNode (Blob const& data, SHAMapAddNode&);
    bool takeAsNode (const std::list<SHAMapNode>& IDs, const std::list<Blob >& data,
                     SHAMapAddNode&);
    bool takeAsRootNode (Blob const& data, SHAMapAddNode&);
    void trigger (Peer::ref);
    bool tryLocal ();
    void addPeers ();
    void awaitData ();
    void noAwaitData ();
    void checkLocal ();

    typedef std::pair <protocol::TMGetObjectByHash::ObjectType, uint256> neededHash_t;

    std::vector<neededHash_t> getNeededHashes ();

    static void filterNodes (std::vector<SHAMapNode>& nodeIDs, std::vector<uint256>& nodeHashes,
                             std::set<SHAMapNode>& recentNodes, int max, bool aggressive);

    Json::Value getJson (int);

private:
    void done ();

    void onTimer (bool progress);

    void newPeer (Peer::ref peer)
    {
        trigger (peer);
    }

    boost::weak_ptr <PeerSet> pmDowncast ();

private:
    Ledger::pointer mLedger;
    bool            mHaveBase;
    bool            mHaveState;
    bool            mHaveTransactions;
    bool            mAborted;
    bool            mSignaled;
    bool            mAccept;
    bool            mByHash;
    int             mWaitCount;
    uint32          mSeq;

    std::set <SHAMapNode> mRecentTXNodes;
    std::set <SHAMapNode> mRecentASNodes;

    std::vector <FUNCTION_TYPE <void (InboundLedger::pointer)> > mOnComplete;
};

#endif

// vim:ts=4
