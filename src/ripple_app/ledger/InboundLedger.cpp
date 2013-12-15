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

SETUP_LOG (InboundLedger)

// VFALCO TODO replace macros
#define LA_DEBUG
#define LEDGER_ACQUIRE_TIMEOUT      6000    // millisecond for each ledger timeout
#define LEDGER_TIMEOUT_COUNT        10      // how many timeouts before we giveup
#define LEDGER_TIMEOUT_AGGRESSIVE   6       // how many timeouts before we get aggressive

InboundLedger::InboundLedger (uint256 const& hash, uint32 seq)
    : PeerSet (hash, LEDGER_ACQUIRE_TIMEOUT, false)
    , mHaveBase (false)
    , mHaveState (false)
    , mHaveTransactions (false)
    , mAborted (false)
    , mSignaled (false)
    , mByHash (true)
    , mSeq (seq)
    , mReceiveDispatched (false)
{
    WriteLog (lsTRACE, InboundLedger) << "Acquiring ledger " << mHash;
}

bool InboundLedger::checkLocal ()
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    if (!isDone () && tryLocal())
    {
        done();
        return true;
    }
    return false;
}

InboundLedger::~InboundLedger ()
{
    BOOST_FOREACH (PeerDataPairType& entry, mReceivedData)
    {
        if (entry.second->type () == protocol::liAS_NODE)
            getApp().getInboundLedgers().gotStaleData(entry.second);
    }

}

void InboundLedger::init(ScopedLockType& collectionLock, bool couldBeNew)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    collectionLock.unlock ();

    if (!tryLocal ())
    {
        addPeers ();
        setTimer ();
    }
    else if (!isFailed ())
    {
        WriteLog (lsDEBUG, InboundLedger) << "Acquiring ledger we already have locally: " << getHash ();
        mLedger->setClosed ();
        mLedger->setImmutable ();
        getApp ().getLedgerMaster ().storeLedger (mLedger);
        if (couldBeNew)
            getApp ().getLedgerMaster ().checkAccept (mLedger);
    }
}

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

            WriteLog (lsTRACE, InboundLedger) << "Ledger base found in fetch pack";
            mLedger = boost::make_shared<Ledger> (data, true);
            getApp().getNodeStore ().store (hotLEDGER, mLedger->getLedgerSeq (), data, mHash);
        }
        else
        {
            mLedger = boost::make_shared<Ledger> (strCopy (node->getData ()), true);
        }

        if (mLedger->getHash () != mHash)
        {
            // We know for a fact the ledger can never be acquired
            WriteLog (lsWARNING, InboundLedger) << mHash << " cannot be a ledger";
            mFailed = true;
            return true;
        }

        mHaveBase = true;
    }

    if (!mHaveTransactions)
    {
        if (mLedger->getTransHash ().isZero ())
        {
            WriteLog (lsTRACE, InboundLedger) << "No TXNs to fetch";
            mHaveTransactions = true;
        }
        else
        {
            TransactionStateSF filter (mLedger->getLedgerSeq ());

            if (mLedger->peekTransactionMap ()->fetchRoot (mLedger->getTransHash (), &filter))
            {
                std::vector<uint256> h = mLedger->getNeededTransactionHashes (1, &filter);

                if (h.empty ())
                {
                    WriteLog (lsTRACE, InboundLedger) << "Had full txn map locally";
                    mHaveTransactions = true;
                }
            }
        }
    }

    if (!mHaveState)
    {
        if (mLedger->getAccountHash ().isZero ())
        {
            WriteLog (lsFATAL, InboundLedger) << "We are acquiring a ledger with a zero account hash";
            mHaveState = true;
        }
        else
        {
            AccountStateSF filter (mLedger->getLedgerSeq ());

            if (mLedger->peekAccountStateMap ()->fetchRoot (mLedger->getAccountHash (), &filter))
            {
                std::vector<uint256> h = mLedger->getNeededAccountStateHashes (1, &filter);

                if (h.empty ())
                {
                    WriteLog (lsTRACE, InboundLedger) << "Had full AS map locally";
                    mHaveState = true;
                }
            }
        }
    }

    if (mHaveTransactions && mHaveState)
    {
        WriteLog (lsDEBUG, InboundLedger) << "Had everything locally";
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
        WriteLog (lsINFO, InboundLedger) << "Already done " << mHash;
        return;
    }

    if (getTimeouts () > LEDGER_TIMEOUT_COUNT)
    {
        if (mSeq != 0)
            WriteLog (lsWARNING, InboundLedger) << getTimeouts() << " timeouts for ledger " << mSeq;
        else
            WriteLog (lsWARNING, InboundLedger) << getTimeouts() << " timeouts for ledger " << mHash;
        setFailed ();
        done ();
        return;
    }

    if (!wasProgress)
    {
        checkLocal();
        if (isDone())
        {
            WriteLog (lsINFO, InboundLedger) << "Completed fetch " << mHash;
            return;
        }

        mAggressive = true;
        mByHash = true;
        int pc = getPeerCount ();
        WriteLog (lsDEBUG, InboundLedger) << "No progress(" << pc << ") for ledger " << mHash;

        trigger (Peer::pointer ());
        if (pc < 4)
            addPeers ();
    }
}

void InboundLedger::addPeers ()
{
    std::vector<Peer::pointer> peerList = getApp().getPeers ().getPeerVector ();

    int vSize = peerList.size ();

    if (vSize == 0)
        return;

    // We traverse the peer list in random order so as not to favor any particular peer
    int firstPeer = rand () % vSize;

    int found = 0;

    for (int i = 0; i < vSize; ++i)
    {
        Peer::ref peer = peerList[ (i + firstPeer) % vSize];

        if (peer->hasLedger (getHash (), mSeq))
        {
           if (peerHas (peer) && (++found > 6))
               break;
        }
    }

    if (!found)
    {
        for (int i = 0; (i < 6) && (i < vSize); ++i)
        {
            if (peerHas (peerList[ (i + firstPeer) % vSize]))
                ++found;
        }
        if (mSeq != 0)
            WriteLog (lsDEBUG, InboundLedger) << "Chose " << found << " peer(s) for ledger " << mSeq;
        else
            WriteLog (lsDEBUG, InboundLedger) << "Chose " << found << " peer(s) for ledger " << getHash ().GetHex();
    }
    else if (mSeq != 0)
        WriteLog (lsDEBUG, InboundLedger) << "Found " << found << " peer(s) with ledger " << mSeq;
    else
        WriteLog (lsDEBUG, InboundLedger) << "Found " << found << " peer(s) with ledger " << getHash ().GetHex();
}

boost::weak_ptr<PeerSet> InboundLedger::pmDowncast ()
{
    return boost::dynamic_pointer_cast<PeerSet> (shared_from_this ());
}

static void LADispatch (
    Job& job,
    InboundLedger::pointer la,
    std::vector< FUNCTION_TYPE<void (InboundLedger::pointer)> > trig)
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

    WriteLog (lsTRACE, InboundLedger) << "Done acquiring ledger " << mHash;

    assert (isComplete () || isFailed ());

    std::vector< FUNCTION_TYPE<void (InboundLedger::pointer)> > triggers;
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
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
                                    BIND_TYPE (LADispatch, P_1, shared_from_this (), triggers));
}

bool InboundLedger::addOnComplete (FUNCTION_TYPE<void (InboundLedger::pointer)> triggerFunc)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    if (isDone ())
        return false;

    mOnComplete.push_back (triggerFunc);
    return true;
}

void InboundLedger::trigger (Peer::ref peer)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    if (isDone ())
    {
        WriteLog (lsDEBUG, InboundLedger) << "Trigger on ledger: " << mHash <<
                                          (mAborted ? " aborted" : "") << (mComplete ? " completed" : "") << (mFailed ? " failed" : "");
        return;
    }

    if (ShouldLog (lsTRACE, InboundLedger))
    {
        if (peer)
            WriteLog (lsTRACE, InboundLedger) << "Trigger acquiring ledger " << mHash << " from " << peer->getIP ();
        else
            WriteLog (lsTRACE, InboundLedger) << "Trigger acquiring ledger " << mHash;

        if (mComplete || mFailed)
            WriteLog (lsTRACE, InboundLedger) << "complete=" << mComplete << " failed=" << mFailed;
        else
            WriteLog (lsTRACE, InboundLedger) << "base=" << mHaveBase << " tx=" << mHaveTransactions << " as=" << mHaveState;
    }

    if (!mHaveBase)
    {
        tryLocal ();

        if (mFailed)
        {
            WriteLog (lsWARNING, InboundLedger) << " failed local for " << mHash;
            return;
        }
    }

    protocol::TMGetLedger tmGL;
    tmGL.set_ledgerhash (mHash.begin (), mHash.size ());

    if (getTimeouts () != 0)
    {
        tmGL.set_querytype (protocol::qtINDIRECT);

        if (!isProgress () && !mFailed && mByHash && (getTimeouts () > LEDGER_TIMEOUT_AGGRESSIVE))
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
                    WriteLog (lsWARNING, InboundLedger) << "Want: " << p.second;

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
                PackedMessage::pointer packet = boost::make_shared<PackedMessage> (tmBH, protocol::mtGET_OBJECTS);
                {
                    ScopedLockType sl (mLock, __FILE__, __LINE__);

                    for (boost::unordered_map<uint64, int>::iterator it = mPeers.begin (), end = mPeers.end ();
                            it != end; ++it)
                    {
                        Peer::pointer iPeer = getApp().getPeers ().getPeerById (it->first);

                        if (iPeer)
                        {
                            mByHash = false;
                            iPeer->sendPacket (packet, false);
                        }
                    }
                }
                WriteLog (lsINFO, InboundLedger) << "Attempting by hash fetch for ledger " << mHash;
            }
            else
            {
                WriteLog (lsINFO, InboundLedger) << "getNeededHashes says acquire is complete";
                mHaveBase = true;
                mHaveTransactions = true;
                mHaveState = true;
                mComplete = true;
            }
        }
    }

    if (!mHaveBase && !mFailed)
    {
        tmGL.set_itype (protocol::liBASE);
        WriteLog (lsTRACE, InboundLedger) << "Sending base request to " << (peer ? "selected peer" : "all peers");
        sendRequest (tmGL, peer);
        return;
    }

    if (mLedger)
        tmGL.set_ledgerseq (mLedger->getLedgerSeq ());

    if (mHaveBase && !mHaveTransactions && !mFailed)
    {
        assert (mLedger);

        if (mLedger->peekTransactionMap ()->getHash ().isZero ())
        {
            // we need the root node
            tmGL.set_itype (protocol::liTX_NODE);
            * (tmGL.add_nodeids ()) = SHAMapNode ().getRawString ();
            WriteLog (lsTRACE, InboundLedger) << "Sending TX root request to " << (peer ? "selected peer" : "all peers");
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
            mLedger->peekTransactionMap ()->getMissingNodes (nodeIDs, nodeHashes, 256, &filter);

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
                    filterNodes (nodeIDs, nodeHashes, mRecentTXNodes, 128, !isProgress ());

                if (!nodeIDs.empty ())
                {
                    tmGL.set_itype (protocol::liTX_NODE);
                    BOOST_FOREACH (SHAMapNode const& it, nodeIDs)
                    {
                        * (tmGL.add_nodeids ()) = it.getRawString ();
                    }
                    WriteLog (lsTRACE, InboundLedger) << "Sending TX node " << nodeIDs.size ()
                                                      << " request to " << (peer ? "selected peer" : "all peers");
                    sendRequest (tmGL, peer);
                    return;
                }
            }
        }
    }

    if (mHaveBase && !mHaveState && !mFailed)
    {
        assert (mLedger);

        if (mLedger->peekAccountStateMap ()->getHash ().isZero ())
        {
            // we need the root node
            tmGL.set_itype (protocol::liAS_NODE);
            * (tmGL.add_nodeids ()) = SHAMapNode ().getRawString ();
            WriteLog (lsTRACE, InboundLedger) << "Sending AS root request to " << (peer ? "selected peer" : "all peers");
            sendRequest (tmGL, peer);
            return;
        }
        else
        {
            std::vector<SHAMapNode> nodeIDs;
            std::vector<uint256> nodeHashes;
            nodeIDs.reserve (256);
            nodeHashes.reserve (256);
            AccountStateSF filter (mSeq);
            mLedger->peekAccountStateMap ()->getMissingNodes (nodeIDs, nodeHashes, 256, &filter);

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
                if (!mAggressive)
                    filterNodes (nodeIDs, nodeHashes, mRecentASNodes, 128, !isProgress ());

                if (!nodeIDs.empty ())
                {
                    tmGL.set_itype (protocol::liAS_NODE);
                    BOOST_FOREACH (SHAMapNode const& it, nodeIDs)
                    {
                        * (tmGL.add_nodeids ()) = it.getRawString ();
                    }
                    WriteLog (lsTRACE, InboundLedger) << "Sending AS node " << nodeIDs.size ()
                                                      << " request to " << (peer ? "selected peer" : "all peers");
                    CondLog (nodeIDs.size () == 1, lsTRACE, InboundLedger) << "AS node: " << nodeIDs[0];
                    sendRequest (tmGL, peer);
                    return;
                }
            }
        }
    }

    if (mComplete || mFailed)
    {
        WriteLog (lsDEBUG, InboundLedger) << "Done:" << (mComplete ? " complete" : "") << (mFailed ? " failed " : " ")
                                          << mLedger->getLedgerSeq ();
        sl.unlock ();
        done ();
    }
}

void InboundLedger::filterNodes (std::vector<SHAMapNode>& nodeIDs, std::vector<uint256>& nodeHashes,
                                 std::set<SHAMapNode>& recentNodes, int max, bool aggressive)
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
            WriteLog (lsTRACE, InboundLedger) << "filterNodes: all are duplicates";
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

        WriteLog (lsTRACE, InboundLedger) << "filterNodes " << nodeIDs.size () << " to " << insertPoint;
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
bool InboundLedger::takeBase (const std::string& data) // data must not have hash prefix
{
    // Return value: true=normal, false=bad data
#ifdef LA_DEBUG
    WriteLog (lsTRACE, InboundLedger) << "got base acquiring ledger " << mHash;
#endif

    if (mComplete || mFailed || mHaveBase)
        return true;

    mLedger = boost::make_shared<Ledger> (data, false);

    if (mLedger->getHash () != mHash)
    {
        WriteLog (lsWARNING, InboundLedger) << "Acquire hash mismatch";
        WriteLog (lsWARNING, InboundLedger) << mLedger->getHash () << "!=" << mHash;
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
    getApp().getNodeStore ().store (hotLEDGER, mLedger->getLedgerSeq (), s.modData (), mHash);

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
        WriteLog (lsWARNING, InboundLedger) << "TX node without base";
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
            san += mLedger->peekTransactionMap ()->addRootNode (mLedger->getTransHash (), *nodeDatait,
                              snfWIRE, &tFilter);
            if (!san.isGood())
                return false;
        }
        else
        {
            san +=  mLedger->peekTransactionMap ()->addKnownNode (*nodeIDit, *nodeDatait, &tFilter);
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
    WriteLog (lsTRACE, InboundLedger) << "got ASdata (" << nodeIDs.size () << ") acquiring ledger " << mHash;
    CondLog (nodeIDs.size () == 1, lsTRACE, InboundLedger) << "got AS node: " << nodeIDs.front ();

    ScopedLockType sl (mLock, __FILE__, __LINE__);

    if (!mHaveBase)
    {
        WriteLog (lsWARNING, InboundLedger) << "Don't have ledger base";
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
            san += mLedger->peekAccountStateMap ()
                ->addRootNode (mLedger->getAccountHash (), *nodeDatait, snfWIRE, &tFilter);
            if (!san.isGood ())
            {
                WriteLog (lsWARNING, InboundLedger) << "Bad ledger base";
                return false;
            }
        }
        else
        {
            san += mLedger->peekAccountStateMap ()->addKnownNode (*nodeIDit, *nodeDatait, &tFilter);
            if (!san.isGood ())
            {
                WriteLog (lsWARNING, InboundLedger) << "Unable to add AS node";
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
        san.incInvalid();
        return false;
    }

    AccountStateSF tFilter (mLedger->getLedgerSeq ());
    san += mLedger->peekAccountStateMap ()->addRootNode (mLedger->getAccountHash (), data, snfWIRE, &tFilter);
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
        san.incInvalid();
        return false;
    }

    TransactionStateSF tFilter (mLedger->getLedgerSeq ());
    san += mLedger->peekTransactionMap ()->addRootNode (mLedger->getTransHash (), data, snfWIRE, &tFilter);
    return san.isGood();
}

std::vector<InboundLedger::neededHash_t> InboundLedger::getNeededHashes ()
{
    std::vector<neededHash_t> ret;

    if (!mHaveBase)
    {
        ret.push_back (std::make_pair (protocol::TMGetObjectByHash::otLEDGER, mHash));
        return ret;
    }

    if (!mHaveState)
    {
        AccountStateSF filter (mLedger->getLedgerSeq ());
        std::vector<uint256> v = mLedger->getNeededAccountStateHashes (4, &filter);
        BOOST_FOREACH (uint256 const & h, v)
        {
            ret.push_back (std::make_pair (protocol::TMGetObjectByHash::otSTATE_NODE, h));
        }
    }

    if (!mHaveTransactions)
    {
        TransactionStateSF filter (mLedger->getLedgerSeq ());
        std::vector<uint256> v = mLedger->getNeededTransactionHashes (4, &filter);
        BOOST_FOREACH (uint256 const & h, v)
        {
            ret.push_back (std::make_pair (protocol::TMGetObjectByHash::otTRANSACTION_NODE, h));
        }
    }

    return ret;
}

/** Stash a TMLedgerData received from a peer for later processing
    Returns 'true' if we need to dispatch
*/
bool InboundLedger::gotData (boost::weak_ptr<Peer> peer, boost::shared_ptr<protocol::TMLedgerData> data)
{
    ScopedLockType sl (mReceivedDataLock, __FILE__, __LINE__);

    mReceivedData.push_back (PeerDataPairType (peer, data));

    if (mReceiveDispatched)
        return false;

    mReceiveDispatched = true;
    return true;
}

/** Process one TMLedgerData
    Returns the number of useful nodes
*/
int InboundLedger::processData (boost::shared_ptr<Peer> peer, protocol::TMLedgerData& packet)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    if (packet.type () == protocol::liBASE)
    {
        if (packet.nodes_size () < 1)
        {
            WriteLog (lsWARNING, InboundLedger) << "Got empty base data";
            peer->charge (Resource::feeInvalidRequest);
            return -1;
        }

        if (!mHaveBase && !takeBase (packet.nodes (0).nodedata ()))
        {
            WriteLog (lsWARNING, InboundLedger) << "Got invalid base data";
            peer->charge (Resource::feeInvalidRequest);
            return -1;
        }

        SHAMapAddNode san;

        if (!mHaveState && (packet.nodes ().size () > 1) &&
            !takeAsRootNode (strCopy (packet.nodes (1).nodedata ()), san))
        {
            WriteLog (lsWARNING, InboundLedger) << "Included ASbase invalid";
        }

        if (!mHaveTransactions && (packet.nodes ().size () > 2) &&
            !takeTxRootNode (strCopy (packet.nodes (2).nodedata ()), san))
        {
            WriteLog (lsWARNING, InboundLedger) << "Included TXbase invalid";
        }

        if (!san.isInvalid ())
            progress ();
        else
            WriteLog (lsDEBUG, InboundLedger) << "Peer sends invalid base data";

        return san.getGood ();
    }

    if ((packet.type () == protocol::liTX_NODE) || (packet.type () == protocol::liAS_NODE))
    {
        std::list<SHAMapNode> nodeIDs;
        std::list< Blob > nodeData;

        if (packet.nodes ().size () == 0)
        {
            WriteLog (lsINFO, InboundLedger) << "Got response with no nodes";
            peer->charge (Resource::feeInvalidRequest);
            return -1;
        }

        for (int i = 0; i < packet.nodes ().size (); ++i)
        {
            const protocol::TMLedgerNode& node = packet.nodes (i);

            if (!node.has_nodeid () || !node.has_nodedata ())
            {
                WriteLog (lsWARNING, InboundLedger) << "Got bad node";
                peer->charge (Resource::feeInvalidRequest);
                return -1;
            }

            nodeIDs.push_back (SHAMapNode (node.nodeid ().data (), node.nodeid ().size ()));
            nodeData.push_back (Blob (node.nodedata ().begin (), node.nodedata ().end ()));
        }

        SHAMapAddNode ret;

        if (packet.type () == protocol::liTX_NODE)
        {
            takeTxNode (nodeIDs, nodeData, ret);
            WriteLog (lsDEBUG, InboundLedger) << "Ledger TX node stats: " << ret.get();
        }
        else
        {
            takeAsNode (nodeIDs, nodeData, ret);
            WriteLog (lsDEBUG, InboundLedger) << "Ledger AS node stats: " << ret.get();
        }

        if (!ret.isInvalid ())
            progress ();
        else
            WriteLog (lsDEBUG, InboundLedger) << "Peer sends invalid node data";

        return ret.getGood ();
    }

    return -1;
}

/** Process pending TMLedgerData
    Query the 'best' peer
*/
void InboundLedger::runData ()
{
    boost::shared_ptr<Peer> chosenPeer;
    int chosenPeerCount = -1;

    std::vector <PeerDataPairType> data;
    do
    {

        data.clear();
        {
            ScopedLockType sl (mReceivedDataLock, __FILE__, __LINE__);

            if (mReceivedData.empty ())
            {
                mReceiveDispatched = false;
                break;
            }
            data.swap(mReceivedData);
        }

        BOOST_FOREACH (PeerDataPairType& entry, data)
        {
            Peer::pointer peer = entry.first.lock();
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

    ScopedLockType sl (mLock, __FILE__, __LINE__);

    ret["hash"] = mHash.GetHex ();

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
        std::vector<uint256> v = mLedger->getNeededAccountStateHashes (16, NULL);
        BOOST_FOREACH (uint256 const & h, v)
        {
            hv.append (h.GetHex ());
        }
        ret["needed_state_hashes"] = hv;
    }

    if (mHaveBase && !mHaveTransactions)
    {
        Json::Value hv (Json::arrayValue);
        std::vector<uint256> v = mLedger->getNeededTransactionHashes (16, NULL);
        BOOST_FOREACH (uint256 const & h, v)
        {
            hv.append (h.GetHex ());
        }
        ret["needed_transaction_hashes"] = hv;
    }

    return ret;
}
