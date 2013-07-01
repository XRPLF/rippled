//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (InboundLedger)

// VFALCO TODO replace macros
#define LA_DEBUG
#define LEDGER_ACQUIRE_TIMEOUT      2000    // millisecond for each ledger timeout
#define LEDGER_TIMEOUT_COUNT        10      // how many timeouts before we giveup
#define LEDGER_TIMEOUT_AGGRESSIVE   6       // how many timeouts before we get aggressive
#define TRUST_NETWORK

InboundLedger::InboundLedger (uint256 const& hash, uint32 seq)
    : PeerSet (hash, LEDGER_ACQUIRE_TIMEOUT)
    , mHaveBase (false)
    , mHaveState (false)
    , mHaveTransactions (false)
    , mAborted (false)
    , mSignaled (false)
    , mAccept (false)
    , mByHash (true)
    , mWaitCount (0)
    , mSeq (seq)
{
#ifdef LA_DEBUG
    WriteLog (lsTRACE, InboundLedger) << "Acquiring ledger " << mHash;
#endif
    tryLocal ();
}

void InboundLedger::checkLocal ()
{
    boost::recursive_mutex::scoped_lock sl (mLock);

    if (isDone ())
        return;

    if (tryLocal ())
        done ();
}

bool InboundLedger::tryLocal ()
{
    // return value: true = no more work to do

    if (!mHaveBase)
    {
        // Nothing we can do without the ledger base
        HashedObject::pointer node = getApp().getHashedObjectStore ().retrieve (mHash);

        if (!node)
        {
            Blob data;

            if (!getApp().getOPs ().getFetchPack (mHash, data))
                return false;

            WriteLog (lsTRACE, InboundLedger) << "Ledger base found in fetch pack";
            mLedger = boost::make_shared<Ledger> (data, true);
            getApp().getHashedObjectStore ().store (hotLEDGER, mLedger->getLedgerSeq (), data, mHash);
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
                WriteLog (lsTRACE, InboundLedger) << "Got root txn map locally";
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
                WriteLog (lsTRACE, InboundLedger) << "Got root AS map locally";
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

void InboundLedger::onTimer (bool progress)
{
    mRecentTXNodes.clear ();
    mRecentASNodes.clear ();

    if (getTimeouts () > LEDGER_TIMEOUT_COUNT)
    {
        WriteLog (lsWARNING, InboundLedger) << "Too many timeouts( " << getTimeouts () << ") for ledger " << mHash;
        setFailed ();
        done ();
        return;
    }

    if (!progress)
    {
        mAggressive = true;
        mByHash = true;
        int pc = getPeerCount ();
        WriteLog (lsDEBUG, InboundLedger) << "No progress(" << pc << ") for ledger " << pc <<  mHash;

        if (pc == 0)
            addPeers ();
        else
            trigger (Peer::pointer ());
    }
}

void InboundLedger::awaitData ()
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    ++mWaitCount;
}

void InboundLedger::noAwaitData ()
{
    boost::recursive_mutex::scoped_lock sl (mLock);

    if (mWaitCount > 0 ) --mWaitCount;
}

void InboundLedger::addPeers ()
{
    std::vector<Peer::pointer> peerList = getApp().getPeers ().getPeerVector ();

    int vSize = peerList.size ();

    if (vSize == 0)
        return;

    // We traverse the peer list in random order so as not to favor any particular peer
    int firstPeer = rand () & vSize;

    int found = 0;

    for (int i = 0; i < vSize; ++i)
    {
        Peer::ref peer = peerList[ (i + firstPeer) % vSize];

        if (peer->hasLedger (getHash (), mSeq))
        {
            peerHas (peer);

            if (++found == 3)
                break;
        }
    }

    if (!found)
        for (int i = 0; i < vSize; ++i)
            peerHas (peerList[ (i + firstPeer) % vSize]);
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
    for (unsigned int i = 0; i < trig.size (); ++i)
        trig[i] (la);
}

void InboundLedger::done ()
{
    if (mSignaled)
        return;

    mSignaled = true;
    touch ();

#ifdef LA_DEBUG
    WriteLog (lsTRACE, InboundLedger) << "Done acquiring ledger " << mHash;
#endif

    assert (isComplete () || isFailed ());

    std::vector< FUNCTION_TYPE<void (InboundLedger::pointer)> > triggers;
    {
        boost::recursive_mutex::scoped_lock sl (mLock);
        triggers.swap (mOnComplete);
    }

    if (isComplete () && !isFailed () && mLedger)
    {
        mLedger->setClosed ();
        mLedger->setImmutable ();

        if (mAccept)
            mLedger->setAccepted ();

        getApp().getLedgerMaster ().storeLedger (mLedger);
    }
    else
        getApp().getInboundLedgers ().logFailure (mHash);

    if (!triggers.empty ()) // We hold the PeerSet lock, so must dispatch
        getApp().getJobQueue ().addJob (jtLEDGER_DATA, "triggers",
                                       BIND_TYPE (LADispatch, P_1, shared_from_this (), triggers));
}

bool InboundLedger::addOnComplete (FUNCTION_TYPE<void (InboundLedger::pointer)> trigger)
{
    boost::recursive_mutex::scoped_lock sl (mLock);

    if (isDone ())
        return false;

    mOnComplete.push_back (trigger);
    return true;
}

void InboundLedger::trigger (Peer::ref peer)
{
    boost::recursive_mutex::scoped_lock sl (mLock);

    if (isDone ())
    {
        WriteLog (lsDEBUG, InboundLedger) << "Trigger on ledger: " << mHash <<
                                          (mAborted ? " aborted" : "") << (mComplete ? " completed" : "") << (mFailed ? " failed" : "");
        return;
    }

    if ((mWaitCount > 0) && peer)
    {
        WriteLog (lsTRACE, InboundLedger) << "Skipping peer";
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
                    boost::recursive_mutex::scoped_lock sl (mLock);

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

void PeerSet::sendRequest (const protocol::TMGetLedger& tmGL, Peer::ref peer)
{
    if (!peer)
        sendRequest (tmGL);
    else
        peer->sendPacket (boost::make_shared<PackedMessage> (tmGL, protocol::mtGET_LEDGER), false);
}

void PeerSet::sendRequest (const protocol::TMGetLedger& tmGL)
{
    boost::recursive_mutex::scoped_lock sl (mLock);

    if (mPeers.empty ())
        return;

    PackedMessage::pointer packet = boost::make_shared<PackedMessage> (tmGL, protocol::mtGET_LEDGER);

    for (boost::unordered_map<uint64, int>::iterator it = mPeers.begin (), end = mPeers.end (); it != end; ++it)
    {
        Peer::pointer peer = getApp().getPeers ().getPeerById (it->first);

        if (peer)
            peer->sendPacket (packet, false);
    }
}

int PeerSet::takePeerSetFrom (const PeerSet& s)
{
    int ret = 0;
    mPeers.clear ();

    for (boost::unordered_map<uint64, int>::const_iterator it = s.mPeers.begin (), end = s.mPeers.end ();
            it != end; ++it)
    {
        mPeers.insert (std::make_pair (it->first, 0));
        ++ret;
    }

    return ret;
}

int PeerSet::getPeerCount () const
{
    int ret = 0;

    for (boost::unordered_map<uint64, int>::const_iterator it = mPeers.begin (), end = mPeers.end (); it != end; ++it)
        if (getApp().getPeers ().hasPeer (it->first))
            ++ret;

    return ret;
}

void InboundLedger::filterNodes (std::vector<SHAMapNode>& nodeIDs, std::vector<uint256>& nodeHashes,
                                 std::set<SHAMapNode>& recentNodes, int max, bool aggressive)
{
    // ask for new nodes in preference to ones we've already asked for
    assert (nodeIDs.size () == nodeHashes.size ());

    std::vector<bool> duplicates;
    duplicates.reserve (nodeIDs.size ());

    int dupCount = 0;

    for (unsigned int i = 0; i < nodeIDs.size (); ++i)
    {
        bool isDup = recentNodes.count (nodeIDs[i]) != 0;
        duplicates.push_back (isDup);

        if (isDup)
            ++dupCount;
    }

    if (dupCount == nodeIDs.size ())
    {
        // all duplicates
        if (!aggressive)
        {
            nodeIDs.clear ();
            nodeHashes.clear ();
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

bool InboundLedger::takeBase (const std::string& data) // data must not have hash prefix
{
    // Return value: true=normal, false=bad data
#ifdef LA_DEBUG
    WriteLog (lsTRACE, InboundLedger) << "got base acquiring ledger " << mHash;
#endif
    boost::recursive_mutex::scoped_lock sl (mLock);

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
    getApp().getHashedObjectStore ().store (hotLEDGER, mLedger->getLedgerSeq (), s.peekData (), mHash);

    progress ();

    if (!mLedger->getTransHash ())
        mHaveTransactions = true;

    if (!mLedger->getAccountHash ())
        mHaveState = true;

    mLedger->setAcquiring ();
    return true;
}

bool InboundLedger::takeTxNode (const std::list<SHAMapNode>& nodeIDs,
                                const std::list< Blob >& data, SHAMapAddNode& san)
{
    boost::recursive_mutex::scoped_lock sl (mLock);

    if (!mHaveBase)
        return false;

    if (mHaveTransactions || mFailed)
        return true;

    std::list<SHAMapNode>::const_iterator nodeIDit = nodeIDs.begin ();
    std::list< Blob >::const_iterator nodeDatait = data.begin ();
    TransactionStateSF tFilter (mLedger->getLedgerSeq ());

    while (nodeIDit != nodeIDs.end ())
    {
        if (nodeIDit->isRoot ())
        {
            if (!san.combine (mLedger->peekTransactionMap ()->addRootNode (mLedger->getTransHash (), *nodeDatait,
                              snfWIRE, &tFilter)))
                return false;
        }
        else
        {
            if (!san.combine (mLedger->peekTransactionMap ()->addKnownNode (*nodeIDit, *nodeDatait, &tFilter)))
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

bool InboundLedger::takeAsNode (const std::list<SHAMapNode>& nodeIDs,
                                const std::list< Blob >& data, SHAMapAddNode& san)
{
    WriteLog (lsTRACE, InboundLedger) << "got ASdata (" << nodeIDs.size () << ") acquiring ledger " << mHash;
    CondLog (nodeIDs.size () == 1, lsTRACE, InboundLedger) << "got AS node: " << nodeIDs.front ();

    boost::recursive_mutex::scoped_lock sl (mLock);

    if (!mHaveBase)
    {
        WriteLog (lsWARNING, InboundLedger) << "Don't have ledger base";
        return false;
    }

    if (mHaveState || mFailed)
        return true;

    std::list<SHAMapNode>::const_iterator nodeIDit = nodeIDs.begin ();
    std::list< Blob >::const_iterator nodeDatait = data.begin ();
    AccountStateSF tFilter (mLedger->getLedgerSeq ());

    while (nodeIDit != nodeIDs.end ())
    {
        if (nodeIDit->isRoot ())
        {
            if (!san.combine (mLedger->peekAccountStateMap ()->addRootNode (mLedger->getAccountHash (),
                              *nodeDatait, snfWIRE, &tFilter)))
            {
                WriteLog (lsWARNING, InboundLedger) << "Bad ledger base";
                return false;
            }
        }
        else if (!san.combine (mLedger->peekAccountStateMap ()->addKnownNode (*nodeIDit, *nodeDatait, &tFilter)))
        {
            WriteLog (lsWARNING, InboundLedger) << "Unable to add AS node";
            return false;
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

bool InboundLedger::takeAsRootNode (Blob const& data, SHAMapAddNode& san)
{
    boost::recursive_mutex::scoped_lock sl (mLock);

    if (mFailed || mHaveState)
        return true;

    if (!mHaveBase)
        return false;

    AccountStateSF tFilter (mLedger->getLedgerSeq ());
    return san.combine (
               mLedger->peekAccountStateMap ()->addRootNode (mLedger->getAccountHash (), data, snfWIRE, &tFilter));
}

bool InboundLedger::takeTxRootNode (Blob const& data, SHAMapAddNode& san)
{
    boost::recursive_mutex::scoped_lock sl (mLock);

    if (mFailed || mHaveState)
        return true;

    if (!mHaveBase)
        return false;

    TransactionStateSF tFilter (mLedger->getLedgerSeq ());
    return san.combine (
               mLedger->peekTransactionMap ()->addRootNode (mLedger->getTransHash (), data, snfWIRE, &tFilter));
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
        std::vector<uint256> v = mLedger->getNeededAccountStateHashes (4, &filter);
        BOOST_FOREACH (uint256 const & h, v)
        {
            ret.push_back (std::make_pair (protocol::TMGetObjectByHash::otTRANSACTION_NODE, h));
        }
    }

    return ret;
}

Json::Value InboundLedger::getJson (int)
{
    Json::Value ret (Json::objectValue);
    ret["hash"] = mHash.GetHex ();

    if (mComplete)
        ret["complete"] = true;

    if (mFailed)
        ret["failed"] = true;

    ret["have_base"] = mHaveBase;
    ret["have_state"] = mHaveState;
    ret["have_transactions"] = mHaveTransactions;

    if (mAborted)
        ret["aborted"] = true;

    ret["timeouts"] = getTimeouts ();

    if (mHaveBase && !mHaveState)
    {
        Json::Value hv (Json::arrayValue);
        std::vector<uint256> v = mLedger->peekAccountStateMap ()->getNeededHashes (16, NULL);
        BOOST_FOREACH (uint256 const & h, v)
        {
            hv.append (h.GetHex ());
        }
        ret["needed_state_hashes"] = hv;
    }

    if (mHaveBase && !mHaveTransactions)
    {
        Json::Value hv (Json::arrayValue);
        std::vector<uint256> v = mLedger->peekTransactionMap ()->getNeededHashes (16, NULL);
        BOOST_FOREACH (uint256 const & h, v)
        {
            hv.append (h.GetHex ());
        }
        ret["needed_transaction_hashes"] = hv;
    }

    return ret;
}
