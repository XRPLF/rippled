//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

InboundLedger::pointer InboundLedgers::findCreate (uint256 const& hash, uint32 seq)
{
    assert (hash.isNonZero ());
    InboundLedger::pointer ret;

    {
        boost::mutex::scoped_lock sl (mLock);
        InboundLedger::pointer& ptr = mLedgers[hash];

        if (ptr)
        { // FIXME: Should set the sequence if it's not set
            ptr->touch ();
            ret = ptr;
        }
        else
        {
            ptr = ret = boost::make_shared<InboundLedger> (hash, seq);

            if (!ret->isDone ())
            {
                ret->addPeers ();
                ret->setTimer (); // Cannot call in constructor
            }
            else if (ret->isComplete ())
            {
                Ledger::pointer ledger = ret->getLedger ();
                ledger->setClosed ();
                ledger->setImmutable ();
                getApp().getLedgerMaster ().storeLedger (ledger);
                WriteLog (lsDEBUG, InboundLedger) << "Acquiring ledger we already have locally: " << hash;
            }
        }
    }

    return ret;
}

InboundLedger::pointer InboundLedgers::find (uint256 const& hash)
{
    assert (hash.isNonZero ());

    InboundLedger::pointer ret;

    {
        boost::mutex::scoped_lock sl (mLock);

        boost::unordered_map<uint256, InboundLedger::pointer>::iterator it = mLedgers.find (hash);
        if (it != mLedgers.end ())
        {
            it->second->touch ();
            ret = it->second;
        }
    }

    return ret;
}

bool InboundLedgers::hasLedger (uint256 const& hash)
{
    assert (hash.isNonZero ());

    boost::mutex::scoped_lock sl (mLock);
    return mLedgers.find (hash) != mLedgers.end ();
}

void InboundLedgers::dropLedger (uint256 const& hash)
{
    assert (hash.isNonZero ());

    boost::mutex::scoped_lock sl (mLock);
    mLedgers.erase (hash);
}

bool InboundLedgers::awaitLedgerData (uint256 const& ledgerHash)
{
    InboundLedger::pointer ledger = find (ledgerHash);

    if (!ledger)
        return false;

    ledger->awaitData ();
    return true;
}

/*
This gets called when
    "We got some data from an inbound ledger"

inboundLedgerTrigger:
  "What do we do with this partial data?"
  Figures out what to do with the responses to our requests for information.

*/
// means "We got some data from an inbound ledger"
void InboundLedgers::gotLedgerData (Job&, uint256 hash,
        boost::shared_ptr<protocol::TMLedgerData> packet_ptr, boost::weak_ptr<Peer> wPeer)
{
    protocol::TMLedgerData& packet = *packet_ptr;
    Peer::pointer peer = wPeer.lock ();

    WriteLog (lsTRACE, InboundLedger) << "Got data (" << packet.nodes ().size () << ") for acquiring ledger: " << hash;

    InboundLedger::pointer ledger = find (hash);

    if (!ledger)
    {
        WriteLog (lsTRACE, InboundLedger) << "Got data for ledger we're not acquiring";

        if (peer)
            peer->applyLoadCharge (LT_InvalidRequest);

        return;
    }

    ledger->noAwaitData ();

    if (!peer)
        return;

    if (packet.type () == protocol::liBASE)
    {
        if (packet.nodes_size () < 1)
        {
            WriteLog (lsWARNING, InboundLedger) << "Got empty base data";
            peer->applyLoadCharge (LT_InvalidRequest);
            return;
        }

        if (!ledger->takeBase (packet.nodes (0).nodedata ()))
        {
            WriteLog (lsWARNING, InboundLedger) << "Got invalid base data";
            peer->applyLoadCharge (LT_InvalidRequest);
            return;
        }

        SHAMapAddNode san = SHAMapAddNode::useful ();

        if ((packet.nodes ().size () > 1) && !ledger->takeAsRootNode (strCopy (packet.nodes (1).nodedata ()), san))
        {
            WriteLog (lsWARNING, InboundLedger) << "Included ASbase invalid";
        }

        if ((packet.nodes ().size () > 2) && !ledger->takeTxRootNode (strCopy (packet.nodes (2).nodedata ()), san))
        {
            WriteLog (lsWARNING, InboundLedger) << "Included TXbase invalid";
        }

        if (!san.isInvalid ())
        {
            ledger->progress ();
            ledger->trigger (peer);
        }
        else
            WriteLog (lsDEBUG, InboundLedger) << "Peer sends invalid base data";

        return;
    }

    if ((packet.type () == protocol::liTX_NODE) || (packet.type () == protocol::liAS_NODE))
    {
        std::list<SHAMapNode> nodeIDs;
        std::list< Blob > nodeData;

        if (packet.nodes ().size () <= 0)
        {
            WriteLog (lsINFO, InboundLedger) << "Got response with no nodes";
            peer->applyLoadCharge (LT_InvalidRequest);
            return;
        }

        for (int i = 0; i < packet.nodes ().size (); ++i)
        {
            const protocol::TMLedgerNode& node = packet.nodes (i);

            if (!node.has_nodeid () || !node.has_nodedata ())
            {
                WriteLog (lsWARNING, InboundLedger) << "Got bad node";
                peer->applyLoadCharge (LT_InvalidRequest);
                return;
            }

            nodeIDs.push_back (SHAMapNode (node.nodeid ().data (), node.nodeid ().size ()));
            nodeData.push_back (Blob (node.nodedata ().begin (), node.nodedata ().end ()));
        }

        SHAMapAddNode ret;

        if (packet.type () == protocol::liTX_NODE)
            ledger->takeTxNode (nodeIDs, nodeData, ret);
        else
            ledger->takeAsNode (nodeIDs, nodeData, ret);

        if (!ret.isInvalid ())
        {
            ledger->progress ();
            ledger->trigger (peer);
        }
        else
            WriteLog (lsDEBUG, InboundLedger) << "Peer sends invalid node data";

        return;
    }

    WriteLog (lsWARNING, InboundLedger) << "Not sure what ledger data we got";
    peer->applyLoadCharge (LT_InvalidRequest);
}

void InboundLedgers::sweep ()
{
    mRecentFailures.sweep ();

    int now = UptimeTimer::getInstance ().getElapsedSeconds ();
    boost::mutex::scoped_lock sl (mLock);

    boost::unordered_map<uint256, InboundLedger::pointer>::iterator it = mLedgers.begin ();

    while (it != mLedgers.end ())
    {
        if (it->second->getLastAction () > now)
        {
            it->second->touch ();
            ++it;
        }
        else if ((it->second->getLastAction () + 60) < now)
            it = mLedgers.erase (it);
        else
            ++it;
    }
}

int InboundLedgers::getFetchCount (int& timeoutCount)
{
    timeoutCount = 0;
    int ret = 0;

    typedef std::pair<uint256, InboundLedger::pointer> u256_acq_pair;
    std::vector<u256_acq_pair> inboundLedgers;

    {
        boost::mutex::scoped_lock sl (mLock);

        inboundLedgers.reserve(mLedgers.size());
        BOOST_FOREACH (const u256_acq_pair & it, mLedgers)
        {
            inboundLedgers.push_back(it);
        }
    }

    BOOST_FOREACH (const u256_acq_pair & it, inboundLedgers)
    {
        if (it.second->isActive ())
        {
            ++ret;
            timeoutCount += it.second->getTimeouts ();
        }
    }
    return ret;
}

void InboundLedgers::gotFetchPack (Job&)
{
    std::vector<InboundLedger::pointer> acquires;
    {
        boost::mutex::scoped_lock sl (mLock);

        acquires.reserve (mLedgers.size ());
        typedef std::pair<uint256, InboundLedger::pointer> u256_acq_pair;
        BOOST_FOREACH (const u256_acq_pair & it, mLedgers)
        {
            assert (it.second);
            acquires.push_back (it.second);
        }
    }

    BOOST_FOREACH (const InboundLedger::pointer & acquire, acquires)
    {
        acquire->checkLocal ();
    }
}

void InboundLedgers::clearFailures ()
{
    boost::mutex::scoped_lock sl (mLock);

    mRecentFailures.clear();
    mLedgers.clear();
}

Json::Value InboundLedgers::getInfo()
{
    Json::Value ret(Json::objectValue);

    typedef std::pair<uint256, InboundLedger::pointer> u256_acq_pair;
    std::vector<u256_acq_pair> acquires;
    {
        boost::mutex::scoped_lock sl (mLock);

        acquires.reserve (mLedgers.size ());
        BOOST_FOREACH (const u256_acq_pair & it, mLedgers)
        {
            assert (it.second);
            acquires.push_back (it);
        }
    }

    BOOST_FOREACH (const u256_acq_pair& it, acquires)
    {
        uint32 seq = it.second->getSeq();
        if (seq > 1)
            ret[lexicalCastThrow <std::string>(seq)] = it.second->getJson(0);
        else
            ret[it.first.GetHex()] = it.second->getJson(0);
    }

    return ret;
}

// vim:ts=4
