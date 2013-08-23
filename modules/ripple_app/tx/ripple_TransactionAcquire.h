//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TRANSACTIONACQUIRE_H
#define RIPPLE_TRANSACTIONACQUIRE_H

// VFALCO TODO rename to PeerTxRequest
// A transaction set we are trying to acquire
class TransactionAcquire
    : public PeerSet
    , public boost::enable_shared_from_this <TransactionAcquire>
    , public CountedObject <TransactionAcquire>
{
public:
    static char const* getCountedObjectName () { return "TransactionAcquire"; }

    typedef boost::shared_ptr<TransactionAcquire> pointer;

public:
    explicit TransactionAcquire (uint256 const& hash);
    virtual ~TransactionAcquire ()
    {
        ;
    }

    SHAMap::ref getMap ()
    {
        return mMap;
    }

    SHAMapAddNode takeNodes (const std::list<SHAMapNode>& IDs,
                             const std::list< Blob >& data, Peer::ref);

private:
    SHAMap::pointer     mMap;
    bool                mHaveRoot;

    void onTimer (bool progress, ScopedLockType& peerSetLock);
    void newPeer (Peer::ref peer)
    {
        trigger (peer);
    }

    void done ();
    void trigger (Peer::ref);
    boost::weak_ptr<PeerSet> pmDowncast ();
};

#endif
