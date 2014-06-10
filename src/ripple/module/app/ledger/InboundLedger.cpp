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

#include <ripple/overlay/Overlay.h>
#include <ripple/nodestore/Database.h>

namespace ripple {

//SETUP_LOG (InboundLedger)
template <> char const* LogPartition::getPartitionName <InboundLedger> () { return "InLedger"; }

enum
{
    // millisecond for each ledger timeout
    ledgerAcquireTimeoutMillis = 2500

    // how many timeouts before we giveup
    ,ledgerTimeoutRetriesMax = 10

    // how many timeouts before we get aggressive
    ,ledgerBecomeAggressiveThreshold = 6
};

InboundLedger::InboundLedger (uint256 const& hash, std::uint32_t seq, fcReason reason,
    clock_type& clock)
    : PeerSet (hash, ledgerAcquireTimeoutMillis, false, clock,
        LogPartition::getJournal <InboundLedger> ())
    , mHaveBase (false)
    , mHaveState (false)
    , mHaveTransactions (false)
    , mAborted (false)
    , mSignaled (false)
    , mByHash (true)
    , mSeq (seq)
    , mReason (reason)
    , mReceiveDispatched (false)
{

    if (m_journal.trace) m_journal.trace <<
        "Acquiring ledger " << mHash;
}

bool InboundLedger::checkLocal ()
{
    ScopedLockType sl (mLock);

    if (!isDone () && tryLocal())
    {
        done();
        return true;
    }
    return false;
}

InboundLedger::~InboundLedger ()
{
    // Save any received AS data not processed. It could be useful
    // for populating a different ledger
    BOOST_FOREACH (PeerDataPairType& entry, mReceivedData)
    {
        if (entry.second->type () == protocol::liAS_NODE)
            getApp().getInboundLedgers().gotStaleData(entry.second);
    }

}

void InboundLedger::init (ScopedLockType& collectionLock)
{
    ScopedLockType sl (mLock);
    collectionLock.unlock ();

    if (!tryLocal ())
    {
        addPeers ();
        setTimer ();

        // For historical nodes, wait a bit since a
        // fetch pack is probably coming
        if (mReason != fcHISTORY)
            trigger (Peer::ptr ());
    }
    else if (!isFailed ())
    {
        if (m_journal.debug) m_journal.debug <<
            "Acquiring ledger we already have locally: " << getHash ();
        mLedger->setClosed ();
        mLedger->setImmutable ();
        getApp ().getLedgerMaster ().storeLedger (mLedger);

        // Check if this could be a newer fully-validated ledger
        if ((mReason == fcVALIDATION) || (mReason == fcCURRENT) || (mReason ==  fcCONSENSUS))
            getApp ().getLedgerMaster ().checkAccept (mLedger);
    }
}

/** See how much of the ledger data, if any, is
    in our node store
*/
bool InboundLedger::tryLocal ()
{
    // return value: true = no more work to do

    if (!mHaveBase)
    {
        // Nothing we can do without the ledger base
        NodeObject::pointer node = getApp().getNodeStore ().fetch (mHash);

        if (!node)
        {
            Blob data;

            if (!getApp().getOPs ().getFetchPack (mHash, data))
                return false;

            if (m_journal.trace) m_journal.trace <<
                "Ledger base found in fetch pack";
            mLedger = std::make_shared<Ledger> (data, true);
            getApp().getNodeStore ().store (hotLEDGER,
                mLedger->getLedgerSeq (), std::move (data), mHash);
        }
        else
        {
            mLedger = std::make_shared<Ledger> (
                strCopy (node->getData ()), true);
        }

        if (mLedger->getHash () != mHash)
        {
            // We know for a fact the ledger can never be acquired
            if (m_journal.warning) m_journal.warning <<
                mHash << " cannot be a ledger";
            mFailed = true;
            return true;
        }

        mHaveBase = true;
    }

    if (!mHaveTransactions)
    {
        if (mLedger->getTransHash ().isZero ())
        {
            if (m_journal.trace) m_journal.trace <<
                "No TXNs to fetch";
            mHaveTransactions = true;
        }
        else
        {
            TransactionStateSF filter (mLedger->getLedgerSeq ());

            if (mLedger->peekTransactionMap ()->fetchRoot (
                mLedger->getTransHash (), &filter))
            {
                std::vector<uint256> h (mLedger->getNeededTransactionHashes (
                    1, &filter));

                if (h.empty ())
                {
                    if (m_journal.trace) m_journal.trace <<
                        "Had full txn map locally";
                    mHaveTransactions = true;
                }
            }
        }
    }

    if (!mHaveState)
    {
        if (mLedger->getAccountHash ().isZero ())
        {
            if (m_journal.fatal) m_journal.fatal <<
                "We are acquiring a ledger with a zero account hash";
            mFailed = true;
            return true;
        }
        else
        {
            AccountStateSF filter (mLedger->getLedgerSeq ());

            if (mLedger->peekAccountStateMap ()->fetchRoot (
                mLedger->getAccountHash (), &filter))
            {
                std::vector<uint256> h (mLedger->getNeededAccountStateHashes (
                    1, &filter));

                if (h.empty ())
                {
                    if (m_journal.trace) m_journal.trace <<
                        "Had full AS map locally";
                    mHaveState = true;
                }
            }
        }
    }

    if (mHaveTransactions && mHaveState)
    {
        if (m_journal.debug) m_journal.debug <<
            "Had everything locally";
        mComplete = true;
        mLedger->setClosed ();
        mLedger->setImmutable ();
    }

    return mComplete;
}

/** Called with a lock by the PeerSet when the timer expires
*/
void InboundLedger::onTimer (bool wasProgress, ScopedLockType&)
{
    mRecentTXNodes.clear ();
    mRecentASNodes.clear ();

    if (isDone())
    {
        if (m_journal.info) m_journal.info <<
            "Already done " << mHash;
        return;
    }

    if (getTimeouts () > ledgerTimeoutRetriesMax)
    {
        if (mSeq != 0)
        {
            if (m_journal.warning) m_journal.warning <<
                getTimeouts() << " timeouts for ledger " << mSeq;
        }
        else
        {
            if (m_journal.warning) m_journal.warning <<
                getTimeouts() << " timeouts for ledger " << mHash;
        }
        setFailed ();
        done ();
        return;
    }

    if (!wasProgress)
    {
        checkLocal();

        mAggressive = true;
        mByHash = true;

        std::size_t pc = getPeerCount ();
        WriteLog (lsDEBUG, InboundLedger) <<
            "No progress(" << pc <<
            ") for ledger " << mHash;

        trigger (Peer::ptr ());
        if (pc < 4)
            addPeers ();
    }
}

/** Add more peers to the set, if possible */
void InboundLedger::addPeers ()
{
    Overlay::PeerSequence peerList = getApp().overlay ().getActivePeers ();

    int vSize = peerList.size ();

    if (vSize == 0)
    {
        WriteLog (lsERROR, InboundLedger) <<
            "No peers to add for ledger acquisition";
        return;
    }

    // FIXME-NIKB why are we doing this convoluted thing here instead of simply
    // shuffling this vector and then pulling however many entries we need?

    // We traverse the peer list in random order so as not to favor
    // any particular peer
    //
    // VFALCO Use random_shuffle
    //        http://en.cppreference.com/w/cpp/algorithm/random_shuffle
    //
    int firstPeer = rand () % vSize;

    int found = 0;

    // First look for peers that are likely to have this ledger
    for (int i = 0; i < vSize; ++i)
    {
        Peer::ptr const& peer = peerList[ (i + firstPeer) % vSize];

        if (peer->hasLedger (getHash (), mSeq))
        {
           if (peerHas (peer) && (++found > 6))
               break;
        }
    }

    if (!found)
    { // Oh well, try some random peers
        for (int i = 0; (i < 6) && (i < vSize); ++i)
        {
            if (peerHas (peerList[ (i + firstPeer) % vSize]))
                ++found;
        }
        if (mSeq != 0)
        {
            if (m_journal.debug) m_journal.debug <<
                "Chose " << found << " peer(s) for ledger " << mSeq;
        }
        else
        {
            if (m_journal.debug) m_journal.debug <<
                "Chose " << found << " peer(s) for ledger " <<
                    to_string (getHash ());
        }
    }
    else if (mSeq != 0)
    {
        if (m_journal.debug) m_journal.debug <<
            "Found " << found << " peer(s) with ledger " << mSeq;
    }
    else
    {
        if (m_journal.debug) m_journal.debug <<
            "Found " << found << " peer(s) with ledger " <<
                to_string (getHash ());
    }
}

std::weak_ptr<PeerSet> InboundLedger::pmDowncast ()
{
    return std::dynamic_pointer_cast<PeerSet> (shared_from_this ());
}

/** Dispatch acquire completion
*/
static void LADispatch (
    Job& job,
    InboundLedger::pointer la,
    std::vector< std::function<void (InboundLedger::pointer)> > trig)
{
    if (la->isComplete() && !la->isFailed())
    {
        getApp().getLedgerMaster().checkAccept(la->getLedger());
        getApp().getLedgerMaster().tryAdvance();
    }
    for (unsigned int i = 0; i < trig.size (); ++i)
        trig[i] (la);
}

void InboundLedger::done ()
{
    if (mSignaled)
        return;

    mSignaled = true;
    touch ();

    if (m_journal.trace) m_journal.trace <<
        "Done acquiring ledger " << mHash;

    assert (isComplete () || isFailed ());

    std::vector< std::function<void (InboundLedger::pointer)> > triggers;
    {
        ScopedLockType sl (mLock);
        triggers.swap (mOnComplete);
    }

    if (isComplete () && !isFailed () && mLedger)
    {
        mLedger->setClosed ();
        mLedger->setImmutable ();
        getApp().getLedgerMaster ().storeLedger (mLedger);
    }
    else
        getApp().getInboundLedgers ().logFailure (mHash);

    // We hold the PeerSet lock, so must dispatch
    getApp().getJobQueue ().addJob (jtLEDGER_DATA, "triggers",
        std::bind (LADispatch, std::placeholders::_1, shared_from_this (),
                   triggers));
}

bool InboundLedger::addOnComplete (
    std::function <void (InboundLedger::pointer)> triggerFunc)
{
    ScopedLockType sl (mLock);

    if (isDone ())
        return false;

    mOnComplete.push_back (triggerFunc);
    return true;
}

/** Request more nodes, perhaps from a specific peer
*/
void InboundLedger::trigger (Peer::ptr const& peer)
{
    ScopedLockType sl (mLock);

    if (isDone ())
    {
        if (m_journal.debug) m_journal.debug <<
            "Trigger on ledger: " << mHash << (mAborted ? " aborted" : "") <<
                (mComplete ? " completed" : "") << (mFailed ? " failed" : "");
        return;
    }

    if (m_journal.trace)
    {
        if (peer)
            m_journal.trace <<
                "Trigger acquiring ledger " << mHash << " from " << peer;
        else
            m_journal.trace <<
                "Trigger acquiring ledger " << mHash;

        if (mComplete || mFailed)
            m_journal.trace <<
                "complete=" << mComplete << " failed=" << mFailed;
        else
            m_journal.trace <<
                "base=" << mHaveBase << " tx=" << mHaveTransactions <<
                    " as=" << mHaveState;
    }

    if (!mHaveBase)
    {
        tryLocal ();

        if (mFailed)
        {
            if (m_journal.warning) m_journal.warning <<
                " failed local for " << mHash;
            return;
        }
    }

    protocol::TMGetLedger tmGL;
    tmGL.set_ledgerhash (mHash.begin (), mHash.size ());

    if (getTimeouts () != 0)
    { // Be more aggressive if we've timed out at least once
        tmGL.set_querytype (protocol::qtINDIRECT);

        if (!isProgress () && !mFailed && mByHash && (
            getTimeouts () > ledgerBecomeAggressiveThreshold))
        {
            std::vector<neededHash_t> need = getNeededHashes ();

            if (!need.empty ())
            {
                protocol::TMGetObjectByHash tmBH;
                tmBH.set_query (true);
                tmBH.set_ledgerhash (mHash.begin (), mHash.size ());
                bool typeSet = false;
                BOOST_FOREACH (neededHash_t & p, need)
                {
                    if (m_journal.warning) m_journal.warning
                        << "Want: " << p.second;

                    if (!typeSet)
                    {
                        tmBH.set_type (p.first);
                        typeSet = true;
                    }

                    if (p.first == tmBH.type ())
                    {
                        protocol::TMIndexedObject* io = tmBH.add_objects ();
                        io->set_hash (p.second.begin (), p.second.size ());
                    }
                }

                Message::pointer packet (std::make_shared <Message> (
                    tmBH, protocol::mtGET_OBJECTS));
                {
                    ScopedLockType sl (mLock);

                    for (PeerSetMap::iterator it = mPeers.begin (), end = mPeers.end ();
                            it != end; ++it)
                    {
                        Peer::ptr iPeer (
                            getApp().overlay ().findPeerByShortID (it->first));

                        if (iPeer)
                        {
                            mByHash = false;
                            iPeer->send (packet);
                        }
                    }
                }
                if (m_journal.info) m_journal.info <<
                    "Attempting by hash fetch for ledger " << mHash;
            }
            else
            {
                if (m_journal.info) m_journal.info <<
                    "getNeededHashes says acquire is complete";
                mHaveBase = true;
                mHaveTransactions = true;
                mHaveState = true;
                mComplete = true;
            }
        }
    }

    // We can't do much without the base data because we don't know the
    // state or transaction root hashes.
    if (!mHaveBase && !mFailed)
    {
        tmGL.set_itype (protocol::liBASE);
        if (m_journal.trace) m_journal.trace <<
            "Sending base request to " << (peer ? "selected peer" : "all peers");
        sendRequest (tmGL, peer);
        return;
    }

    if (mLedger)
        tmGL.set_ledgerseq (mLedger->getLedgerSeq ());

    // Get the state data first because it's the most likely to be useful
    // if we wind up abandoning this fetch.
    if (mHaveBase && !mHaveState && !mFailed)
    {
        assert (mLedger);

        if (!mLedger->peekAccountStateMap ()->isValid ())
        {
            mFailed = true;
        }
        else if (mLedger->peekAccountStateMap ()->getHash ().isZero ())
        {
            // we need the root node
            tmGL.set_itype (protocol::liAS_NODE);
            * (tmGL.add_nodeids ()) = SHAMapNode ().getRawString ();
            if (m_journal.trace) m_journal.trace <<
                "Sending AS root request to " << (peer ? "selected peer" : "all peers");
            sendRequest (tmGL, peer);
            return;
        }
        else
        {
            std::vector<SHAMapNode> nodeIDs;
            std::vector<uint256> nodeHashes;
            // VFALCO Why 256? Make this a constant
            nodeIDs.reserve (256);
            nodeHashes.reserve (256);
            AccountStateSF filter (mSeq);

            // Release the lock while we process the large state map
            sl.unlock();
            mLedger->peekAccountStateMap ()->getMissingNodes (
                nodeIDs, nodeHashes, 256, &filter);
            sl.lock();

            // Make sure nothing happened while we released the lock
            if (!mFailed && !mComplete && !mHaveState)
            {
                if (nodeIDs.empty ())
                {
                    if (!mLedger->peekAccountStateMap ()->isValid ())
                        mFailed = true;
                    else
                    {
                        mHaveState = true;

                        if (mHaveTransactions)
                            mComplete = true;
                    }
                }
                else
                {
                    // VFALCO Why 128? Make this a constant
                    if (!mAggressive)
                        filterNodes (nodeIDs, nodeHashes, mRecentASNodes,
                            128, !isProgress ());

                    if (!nodeIDs.empty ())
                    {
                        tmGL.set_itype (protocol::liAS_NODE);
                        BOOST_FOREACH (SHAMapNode const& it, nodeIDs)
                        {
                            * (tmGL.add_nodeids ()) = it.getRawString ();
                        }
                        if (m_journal.trace) m_journal.trace <<
                            "Sending AS node " << nodeIDs.size () <<
                                " request to " << (
                                    peer ? "selected peer" : "all peers");
                        if (nodeIDs.size () == 1 && m_journal.trace) m_journal.trace <<
                            "AS node: " << nodeIDs[0];
                        sendRequest (tmGL, peer);
                        return;
                    }
                    else
                        if (m_journal.trace) m_journal.trace <<
                            "All AS nodes filtered";
                }
            }
        }
    }

    if (mHaveBase && !mHaveTransactions && !mFailed)
    {
        assert (mLedger);

        if (!mLedger->peekTransactionMap ()->isValid ())
        {
            mFailed = true;
        }
        else if (mLedger->peekTransactionMap ()->getHash ().isZero ())
        {
            // we need the root node
            tmGL.set_itype (protocol::liTX_NODE);
            * (tmGL.add_nodeids ()) = SHAMapNode ().getRawString ();
            if (m_journal.trace) m_journal.trace <<
                "Sending TX root request to " << (
                    peer ? "selected peer" : "all peers");
            sendRequest (tmGL, peer);
            return;
        }
        else
        {
            std::vector<SHAMapNode> nodeIDs;
            std::vector<uint256> nodeHashes;
            nodeIDs.reserve (256);
            nodeHashes.reserve (256);
            TransactionStateSF filter (mSeq);
            mLedger->peekTransactionMap ()->getMissingNodes (
                nodeIDs, nodeHashes, 256, &filter);

            if (nodeIDs.empty ())
            {
                if (!mLedger->peekTransactionMap ()->isValid ())
                    mFailed = true;
                else
                {
                    mHaveTransactions = true;

                    if (mHaveState)
                        mComplete = true;
                }
            }
            else
            {
                if (!mAggressive)
                    filterNodes (nodeIDs, nodeHashes, mRecentTXNodes,
                        128, !isProgress ());

                if (!nodeIDs.empty ())
                {
                    tmGL.set_itype (protocol::liTX_NODE);
                    BOOST_FOREACH (SHAMapNode const& it, nodeIDs)
                    {
                        * (tmGL.add_nodeids ()) = it.getRawString ();
                    }
                    if (m_journal.trace) m_journal.trace <<
                        "Sending TX node " << nodeIDs.size () <<
                        " request to " << (
                            peer ? "selected peer" : "all peers");
                    sendRequest (tmGL, peer);
                    return;
                }
                else
                    if (m_journal.trace) m_journal.trace <<
                        "All TX nodes filtered";
            }
        }
    }

    if (mComplete || mFailed)
    {
        if (m_journal.debug) m_journal.debug <<
            "Done:" << (mComplete ? " complete" : "") <<
                (mFailed ? " failed " : " ") <<
            mLedger->getLedgerSeq ();
        sl.unlock ();
        done ();
    }
}

void InboundLedger::filterNodes (std::vector<SHAMapNode>& nodeIDs,
    std::vector<uint256>& nodeHashes, std::set<SHAMapNode>& recentNodes,
    int max, bool aggressive)
{
    // ask for new nodes in preference to ones we've already asked for
    assert (nodeIDs.size () == nodeHashes.size ());

    std::vector<bool> duplicates;
    duplicates.reserve (nodeIDs.size ());

    int dupCount = 0;

    BOOST_FOREACH(SHAMapNode const& nodeID, nodeIDs)
    {
        if (recentNodes.count (nodeID) != 0)
        {
            duplicates.push_back (true);
            ++dupCount;
        }
        else
            duplicates.push_back (false);
    }

    if (dupCount == nodeIDs.size ())
    {
        // all duplicates
        if (!aggressive)
        {
            nodeIDs.clear ();
            nodeHashes.clear ();
            if (m_journal.trace) m_journal.trace <<
                "filterNodes: all are duplicates";
            return;
        }
    }
    else if (dupCount > 0)
    {
        // some, but not all, duplicates
        int insertPoint = 0;

        for (unsigned int i = 0; i < nodeIDs.size (); ++i)
            if (!duplicates[i])
            {
                // Keep this node
                if (insertPoint != i)
                {
                    nodeIDs[insertPoint] = nodeIDs[i];
                    nodeHashes[insertPoint] = nodeHashes[i];
                }

                ++insertPoint;
            }

        if (m_journal.trace) m_journal.trace <<
            "filterNodes " << nodeIDs.size () << " to " << insertPoint;
        nodeIDs.resize (insertPoint);
        nodeHashes.resize (insertPoint);
    }

    if (nodeIDs.size () > max)
    {
        nodeIDs.resize (max);
        nodeHashes.resize (max);
    }

    BOOST_FOREACH (const SHAMapNode & n, nodeIDs)
    {
        recentNodes.insert (n);
    }
}

/** Take ledger base data
    Call with a lock
*/
// data must not have hash prefix
bool InboundLedger::takeBase (const std::string& data)
{
    // Return value: true=normal, false=bad data
    if (m_journal.trace) m_journal.trace <<
        "got base acquiring ledger " << mHash;

    if (mComplete || mFailed || mHaveBase)
        return true;

    mLedger = std::make_shared<Ledger> (data, false);

    if (mLedger->getHash () != mHash)
    {
        if (m_journal.warning) m_journal.warning <<
            "Acquire hash mismatch";
        if (m_journal.warning) m_journal.warning <<
            mLedger->getHash () << "!=" << mHash;
        mLedger.reset ();
#ifdef TRUST_NETWORK
        assert (false);
#endif
        return false;
    }

    mHaveBase = true;

    Serializer s (data.size () + 4);
    s.add32 (HashPrefix::ledgerMaster);
    s.addRaw (data);
    getApp().getNodeStore ().store (hotLEDGER,
        mLedger->getLedgerSeq (), std::move (s.modData ()), mHash);

    progress ();

    if (mLedger->getTransHash ().isZero ())
        mHaveTransactions = true;

    if (mLedger->getAccountHash ().isZero ())
        mHaveState = true;

    mLedger->setAcquiring ();
    return true;
}

/** Process TX data received from a peer
    Call with a lock
*/
bool InboundLedger::takeTxNode (const std::list<SHAMapNode>& nodeIDs,
    const std::list< Blob >& data, SHAMapAddNode& san)
{
    if (!mHaveBase)
    {
        if (m_journal.warning) m_journal.warning <<
            "TX node without base";
        san.incInvalid();
        return false;
    }

    if (mHaveTransactions || mFailed)
    {
        san.incDuplicate();
        return true;
    }

    std::list<SHAMapNode>::const_iterator nodeIDit = nodeIDs.begin ();
    std::list< Blob >::const_iterator nodeDatait = data.begin ();
    TransactionStateSF tFilter (mLedger->getLedgerSeq ());

    while (nodeIDit != nodeIDs.end ())
    {
        if (nodeIDit->isRoot ())
        {
            san += mLedger->peekTransactionMap ()->addRootNode (
                mLedger->getTransHash (), *nodeDatait, snfWIRE, &tFilter);
            if (!san.isGood())
                return false;
        }
        else
        {
            san +=  mLedger->peekTransactionMap ()->addKnownNode (
                *nodeIDit, *nodeDatait, &tFilter);
            if (!san.isGood())
                return false;
        }

        ++nodeIDit;
        ++nodeDatait;
    }

    if (!mLedger->peekTransactionMap ()->isSynching ())
    {
        mHaveTransactions = true;

        if (mHaveState)
        {
            mComplete = true;
            done ();
        }
    }

    progress ();
    return true;
}

/** Process AS data received from a peer
    Call with a lock
*/
bool InboundLedger::takeAsNode (const std::list<SHAMapNode>& nodeIDs,
    const std::list< Blob >& data, SHAMapAddNode& san)
{
    if (m_journal.trace) m_journal.trace <<
        "got ASdata (" << nodeIDs.size () << ") acquiring ledger " << mHash;
    if (nodeIDs.size () == 1 && m_journal.trace) m_journal.trace <<
        "got AS node: " << nodeIDs.front ();

    ScopedLockType sl (mLock);

    if (!mHaveBase)
    {
        if (m_journal.warning) m_journal.warning <<
            "Don't have ledger base";
        san.incInvalid();
        return false;
    }

    if (mHaveState || mFailed)
    {
        san.incDuplicate();
        return true;
    }

    std::list<SHAMapNode>::const_iterator nodeIDit = nodeIDs.begin ();
    std::list< Blob >::const_iterator nodeDatait = data.begin ();
    AccountStateSF tFilter (mLedger->getLedgerSeq ());

    while (nodeIDit != nodeIDs.end ())
    {
        if (nodeIDit->isRoot ())
        {
            san += mLedger->peekAccountStateMap ()->addRootNode (
                mLedger->getAccountHash (), *nodeDatait, snfWIRE, &tFilter);
            if (!san.isGood ())
            {
                if (m_journal.warning) m_journal.warning <<
                    "Bad ledger base";
                return false;
            }
        }
        else
        {
            san += mLedger->peekAccountStateMap ()->addKnownNode (
                *nodeIDit, *nodeDatait, &tFilter);
            if (!san.isGood ())
            {
                if (m_journal.warning) m_journal.warning <<
                    "Unable to add AS node";
                return false;
            }
        }

        ++nodeIDit;
        ++nodeDatait;
    }

    if (!mLedger->peekAccountStateMap ()->isSynching ())
    {
        mHaveState = true;

        if (mHaveTransactions)
        {
            mComplete = true;
            done ();
        }
    }

    progress ();
    return true;
}

/** Process AS root node received from a peer
    Call with a lock
*/
bool InboundLedger::takeAsRootNode (Blob const& data, SHAMapAddNode& san)
{
    if (mFailed || mHaveState)
    {
        san.incDuplicate();
        return true;
    }

    if (!mHaveBase)
    {
        assert(false);
        san.incInvalid();
        return false;
    }

    AccountStateSF tFilter (mLedger->getLedgerSeq ());
    san += mLedger->peekAccountStateMap ()->addRootNode (
        mLedger->getAccountHash (), data, snfWIRE, &tFilter);
    return san.isGood();
}

/** Process AS root node received from a peer
    Call with a lock
*/
bool InboundLedger::takeTxRootNode (Blob const& data, SHAMapAddNode& san)
{
    if (mFailed || mHaveState)
    {
        san.incDuplicate();
        return true;
    }

    if (!mHaveBase)
    {
        assert(false);
        san.incInvalid();
        return false;
    }

    TransactionStateSF tFilter (mLedger->getLedgerSeq ());
    san += mLedger->peekTransactionMap ()->addRootNode (
        mLedger->getTransHash (), data, snfWIRE, &tFilter);
    return san.isGood();
}

std::vector<InboundLedger::neededHash_t> InboundLedger::getNeededHashes ()
{
    std::vector<neededHash_t> ret;

    if (!mHaveBase)
    {
        ret.push_back (std::make_pair (
            protocol::TMGetObjectByHash::otLEDGER, mHash));
        return ret;
    }

    if (!mHaveState)
    {
        AccountStateSF filter (mLedger->getLedgerSeq ());
        // VFALCO NOTE What's the number 4?
        std::vector<uint256> v = mLedger->getNeededAccountStateHashes (
            4, &filter);
        BOOST_FOREACH (uint256 const & h, v)
        {
            ret.push_back (std::make_pair (
                protocol::TMGetObjectByHash::otSTATE_NODE, h));
        }
    }

    if (!mHaveTransactions)
    {
        TransactionStateSF filter (mLedger->getLedgerSeq ());
        // VFALCO NOTE What's the number 4?
        std::vector<uint256> v = mLedger->getNeededTransactionHashes (
            4, &filter);
        BOOST_FOREACH (uint256 const & h, v)
        {
            ret.push_back (std::make_pair (
                protocol::TMGetObjectByHash::otTRANSACTION_NODE, h));
        }
    }

    return ret;
}

/** Stash a TMLedgerData received from a peer for later processing
    Returns 'true' if we need to dispatch
*/
// VFALCO TODO Why isn't the shared_ptr passed by const& ?
bool InboundLedger::gotData (std::weak_ptr<Peer> peer,
    std::shared_ptr<protocol::TMLedgerData> data)
{
    ScopedLockType sl (mReceivedDataLock);

    mReceivedData.push_back (PeerDataPairType (peer, data));

    if (mReceiveDispatched)
        return false;

    mReceiveDispatched = true;
    return true;
}

/** Process one TMLedgerData
    Returns the number of useful nodes
*/
// VFALCO NOTE, it is not necessary to pass the entire Peer,
//              we can get away with just a Resource::Consumer endpoint.
//
//        TODO Change peer to Consumer
//
int InboundLedger::processData (std::shared_ptr<Peer> peer,
    protocol::TMLedgerData& packet)
{
    ScopedLockType sl (mLock);

    if (packet.type () == protocol::liBASE)
    {
        if (packet.nodes_size () < 1)
        {
            if (m_journal.warning) m_journal.warning <<
                "Got empty base data";
            peer->charge (Resource::feeInvalidRequest);
            return -1;
        }

        SHAMapAddNode san;

        if (!mHaveBase)
        {
            if (takeBase (packet.nodes (0).nodedata ()))
                san.incUseful ();
            else
            {
                if (m_journal.warning) m_journal.warning <<
                    "Got invalid base data";
                peer->charge (Resource::feeInvalidRequest);
                return -1;
            }
        }


        if (!mHaveState && (packet.nodes ().size () > 1) &&
            !takeAsRootNode (strCopy (packet.nodes (1).nodedata ()), san))
        {
            if (m_journal.warning) m_journal.warning <<
                "Included ASbase invalid";
        }

        if (!mHaveTransactions && (packet.nodes ().size () > 2) &&
            !takeTxRootNode (strCopy (packet.nodes (2).nodedata ()), san))
        {
            if (m_journal.warning) m_journal.warning <<
                "Included TXbase invalid";
        }

        if (!san.isInvalid ())
            progress ();
        else
            if (m_journal.debug) m_journal.debug <<
                "Peer sends invalid base data";

        return san.getGood ();
    }

    if ((packet.type () == protocol::liTX_NODE) || (
        packet.type () == protocol::liAS_NODE))
    {
        std::list<SHAMapNode> nodeIDs;
        std::list< Blob > nodeData;

        if (packet.nodes ().size () == 0)
        {
            if (m_journal.info) m_journal.info <<
                "Got response with no nodes";
            peer->charge (Resource::feeInvalidRequest);
            return -1;
        }

        for (int i = 0; i < packet.nodes ().size (); ++i)
        {
            const protocol::TMLedgerNode& node = packet.nodes (i);

            if (!node.has_nodeid () || !node.has_nodedata ())
            {
                if (m_journal.warning) m_journal.warning <<
                    "Got bad node";
                peer->charge (Resource::feeInvalidRequest);
                return -1;
            }

            nodeIDs.push_back (SHAMapNode (node.nodeid ().data (),
                node.nodeid ().size ()));
            nodeData.push_back (Blob (node.nodedata ().begin (),
                node.nodedata ().end ()));
        }

        SHAMapAddNode ret;

        if (packet.type () == protocol::liTX_NODE)
        {
            takeTxNode (nodeIDs, nodeData, ret);
            if (m_journal.debug) m_journal.debug <<
                "Ledger TX node stats: " << ret.get();
        }
        else
        {
            takeAsNode (nodeIDs, nodeData, ret);
            if (m_journal.debug) m_journal.debug <<
                "Ledger AS node stats: " << ret.get();
        }

        if (!ret.isInvalid ())
            progress ();
        else
            if (m_journal.debug) m_journal.debug <<
                "Peer sends invalid node data";

        return ret.getGood ();
    }

    return -1;
}

/** Process pending TMLedgerData
    Query the 'best' peer
*/
void InboundLedger::runData ()
{
    std::shared_ptr<Peer> chosenPeer;
    int chosenPeerCount = -1;

    std::vector <PeerDataPairType> data;
    do
    {
        data.clear();
        {
            ScopedLockType sl (mReceivedDataLock);

            if (mReceivedData.empty ())
            {
                mReceiveDispatched = false;
                break;
            }
            data.swap(mReceivedData);
        }

        // Select the peer that gives us the most nodes that are useful,
        // breaking ties in favor of the peer that responded first.
        BOOST_FOREACH (PeerDataPairType& entry, data)
        {
            Peer::ptr peer = entry.first.lock();
            if (peer)
            {
                int count = processData (peer, *(entry.second));
                if (count > chosenPeerCount)
                {
                    chosenPeer = peer;
                    chosenPeerCount = count;
                }
            }
        }

    } while (1);

    if (chosenPeer)
        trigger (chosenPeer);
}

Json::Value InboundLedger::getJson (int)
{
    Json::Value ret (Json::objectValue);

    ScopedLockType sl (mLock);

    ret["hash"] = to_string (mHash);

    if (mComplete)
        ret["complete"] = true;

    if (mFailed)
        ret["failed"] = true;

    if (!mComplete && !mFailed)
        ret["peers"] = static_cast<int>(mPeers.size());

    ret["have_base"] = mHaveBase;

    if (mHaveBase)
    {
        ret["have_state"] = mHaveState;
        ret["have_transactions"] = mHaveTransactions;
    }

    if (mAborted)
        ret["aborted"] = true;

    ret["timeouts"] = getTimeouts ();

    if (mHaveBase && !mHaveState)
    {
        Json::Value hv (Json::arrayValue);

        // VFALCO Why 16?
        auto v = mLedger->getNeededAccountStateHashes (16, nullptr);

        for (auto const& h : v)
        {
            hv.append (to_string (h));
        }
        ret["needed_state_hashes"] = hv;
    }

    if (mHaveBase && !mHaveTransactions)
    {
        Json::Value hv (Json::arrayValue);
        // VFALCO Why 16?
        auto v = mLedger->getNeededTransactionHashes (16, nullptr);

        for (auto const& h : v)
        {
            hv.append (to_string (h));
        }
        ret["needed_transaction_hashes"] = hv;
    }

    return ret;
}

} // ripple
