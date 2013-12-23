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

typedef std::pair<uint256, InboundLedger::pointer> u256_acq_pair;

InboundLedgers::InboundLedgers (Stoppable& parent)
    : Stoppable ("InboundLedgers", parent)
    , mLock (this, "InboundLedger", __FILE__, __LINE__)
    , mRecentFailures ("LedgerAcquireRecentFailures", 0, kReacquireIntervalSeconds)
{
}

InboundLedger::pointer InboundLedgers::findCreateConsensusLedger (uint256 const& hash)
{
    // We do not want to destroy the ledger while holding the collection lock
    InboundLedger::pointer oldLedger;

    {
        // If there was an old consensus inbound ledger, remove it
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        if (mConsensusLedger.isNonZero() && (mValidationLedger != mConsensusLedger) && (hash != mConsensusLedger))
        {
            boost::unordered_map<uint256, InboundLedger::pointer>::iterator it = mLedgers.find (mConsensusLedger);
            if (it != mLedgers.end ())
            {
                oldLedger = it->second;
                mLedgers.erase (it);
           }
        }
        mConsensusLedger = hash;
    }

    return findCreate (hash, 0, true);
}

InboundLedger::pointer InboundLedgers::findCreateValidationLedger (uint256 const& hash)
{
    InboundLedger::pointer oldLedger;

    {
        // If there was an old validation inbound ledger, remove it
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        if (mValidationLedger.isNonZero() && (mValidationLedger != mConsensusLedger) && (hash != mValidationLedger))
        {
            boost::unordered_map<uint256, InboundLedger::pointer>::iterator it = mLedgers.find (mValidationLedger);
            if (it != mLedgers.end ())
            {
                oldLedger = it->second;
                mLedgers.erase (it);
           }
        }
        mValidationLedger = hash;
    }

    return findCreate (hash, 0, true);
}

InboundLedger::pointer InboundLedgers::findCreate (uint256 const& hash, uint32 seq, bool couldBeNew)
{
    assert (hash.isNonZero ());
    InboundLedger::pointer ret;

    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);

        if (! isStopping ())
        {
            boost::unordered_map<uint256, InboundLedger::pointer>::iterator it = mLedgers.find (hash);
            if (it != mLedgers.end ())
            {
                ret = it->second;
                // FIXME: Should set the sequence if it's not set
            }
            else
            {
                ret = boost::make_shared<InboundLedger> (hash, seq);
                assert (ret);
                mLedgers.insert (std::make_pair (hash, ret));
                ret->init (sl, couldBeNew);
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
        ScopedLockType sl (mLock, __FILE__, __LINE__);

        boost::unordered_map<uint256, InboundLedger::pointer>::iterator it = mLedgers.find (hash);
        if (it != mLedgers.end ())
        {
            ret = it->second;
        }
    }

    return ret;
}

bool InboundLedgers::hasLedger (LedgerHash const& hash)
{
    assert (hash.isNonZero ());

    ScopedLockType sl (mLock, __FILE__, __LINE__);
    return mLedgers.find (hash) != mLedgers.end ();
}

void InboundLedgers::dropLedger (LedgerHash const& hash)
{
    assert (hash.isNonZero ());

    // We don't want to destroy the ledger while holding the lock
    InboundLedger::pointer ledger;

    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        boost::unordered_map<uint256, InboundLedger::pointer>::iterator it = mLedgers.find (hash);
        if (it != mLedgers.end ())
        {
            ledger = it->second;
            mLedgers.erase (it);
        }
    }

}

/** We received a TMLedgerData from a peer.
*/
bool InboundLedgers::gotLedgerData (LedgerHash const& hash,
        boost::shared_ptr<Peer> peer,
        boost::shared_ptr<protocol::TMLedgerData> packet_ptr)
{
    protocol::TMLedgerData& packet = *packet_ptr;

    WriteLog (lsTRACE, InboundLedger) << "Got data (" << packet.nodes ().size () << ") for acquiring ledger: " << hash;

    InboundLedger::pointer ledger = find (hash);

    if (!ledger)
    {
        WriteLog (lsTRACE, InboundLedger) << "Got data for ledger we're no longer acquiring";

        // If it's state node data, stash it because it still might be useful
        if (packet.type () == protocol::liAS_NODE)
        {
            getApp().getJobQueue().addJob(jtLEDGER_DATA, "gotStaleData",
                BIND_TYPE(&InboundLedgers::gotStaleData, this, packet_ptr));
        }

        return false;
    }

    // Stash the data for later processing and see if we need to dispatch
    if (ledger->gotData(boost::weak_ptr<Peer>(peer), packet_ptr))
        getApp().getJobQueue().addJob (jtLEDGER_DATA, "processLedgerData",
            BIND_TYPE (&InboundLedgers::doLedgerData, this, P_1, hash));

    return true;
}

void InboundLedgers::doLedgerData (Job&, LedgerHash hash)
{
    InboundLedger::pointer ledger = find (hash);

    if (ledger)
        ledger->runData ();
}

/** We got some data for a ledger we are no longer acquiring
    Since we paid the price to receive it, we might as well stash it in case we need it.
    Nodes are received in wire format and must be stashed/hashed in prefix format
*/
void InboundLedgers::gotStaleData (boost::shared_ptr<protocol::TMLedgerData> packet_ptr)
{
    const uint256 uZero;
    try
    {
        for (int i = 0; i < packet_ptr->nodes ().size (); ++i)
        {
            const protocol::TMLedgerNode& node = packet_ptr->nodes (i);

            if (!node.has_nodeid () || !node.has_nodedata ())
                return;

            Serializer s;
            SHAMapTreeNode newNode(
                SHAMapNode (node.nodeid().data(), node.nodeid().size()),
                Blob (node.nodedata().begin(), node.nodedata().end()),
                0, snfWIRE, uZero, false);
            newNode.addRaw(s, snfPREFIX);

            boost::shared_ptr<Blob> blob = boost::make_shared<Blob> (s.begin(), s.end());

            getApp().getOPs().addFetchPack (newNode.getNodeHash(), blob);
        }
    }
    catch (...)
    {
    }
}

void InboundLedgers::sweep ()
{
    mRecentFailures.sweep ();

    int const now = UptimeTimer::getInstance ().getElapsedSeconds ();

    // Make a list of things to sweep, while holding the lock
    std::vector <MapType::mapped_type> stuffToSweep;
    std::size_t total;
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        MapType::iterator it (mLedgers.begin ());
        total = mLedgers.size ();
        stuffToSweep.reserve (total);

        while (it != mLedgers.end ())
        {
            if (it->second->getLastAction () > now)
            {
                it->second->touch ();
                ++it;
            }
            else if ((it->second->getLastAction () + 60) < now)
            {
                stuffToSweep.push_back (it->second);
                // shouldn't cause the actual final delete
                // since we are holding a reference in the vector.
                it = mLedgers.erase (it);
            }
            else
            {
                ++it;
            }
        }
    }

    WriteLog (lsDEBUG, InboundLedger) <<
        "Sweeped " << stuffToSweep.size () <<
        " out of " << total << " inbound ledgers.";
}

int InboundLedgers::getFetchCount (int& timeoutCount)
{
    timeoutCount = 0;
    int ret = 0;

    std::vector<u256_acq_pair> inboundLedgers;

    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);

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
        ScopedLockType sl (mLock, __FILE__, __LINE__);

        acquires.reserve (mLedgers.size ());
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
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    mRecentFailures.clear();
    mLedgers.clear();
}

Json::Value InboundLedgers::getInfo()
{
    Json::Value ret(Json::objectValue);

    std::vector<u256_acq_pair> acquires;
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);

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

void InboundLedgers::onStop ()
{
    ScopedLockType lock (mLock, __FILE__, __LINE__);

    mLedgers.clear();
    mRecentFailures.clear();

    stopped();
}
