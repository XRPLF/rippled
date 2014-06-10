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

namespace ripple {

//SETUP_LOG (TransactionAcquire)
template <> char const* LogPartition::getPartitionName <TransactionAcquire> () { return "TxAcquire"; }

enum
{
    // VFALCO NOTE This should be a std::chrono::duration constant.
    // TODO Document this. Is it seconds? Milliseconds? WTF?
    TX_ACQUIRE_TIMEOUT = 250
};

typedef std::map<uint160, LedgerProposal::pointer>::value_type u160_prop_pair;
typedef std::map<uint256, DisputedTx::pointer>::value_type u256_lct_pair;

TransactionAcquire::TransactionAcquire (uint256 const& hash, clock_type& clock)
    : PeerSet (hash, TX_ACQUIRE_TIMEOUT, true, clock,
        LogPartition::getJournal <TransactionAcquire> ())
    , mHaveRoot (false)
{
    mMap = std::make_shared<SHAMap> (smtTRANSACTION, hash,
        std::ref (getApp().getFullBelowCache ()));
    mMap->setTXMap ();
}

TransactionAcquire::~TransactionAcquire ()
{
}

static void TACompletionHandler (uint256 hash, SHAMap::pointer map)
{
    {
        Application::ScopedLockType lock (getApp ().getMasterLock ());

        getApp().getOPs ().mapComplete (hash, map);

        getApp().getInboundLedgers ().dropLedger (hash);
    }
}

void TransactionAcquire::done ()
{
    // We hold a PeerSet lock and so cannot acquire the master lock here
    SHAMap::pointer map;

    if (mFailed)
    {
        WriteLog (lsWARNING, TransactionAcquire) << "Failed to acquire TX set " << mHash;
    }
    else
    {
        WriteLog (lsINFO, TransactionAcquire) << "Acquired TX set " << mHash;
        mMap->setImmutable ();
        map = mMap;
    }

    getApp().getJobQueue().addJob (jtTXN_DATA, "completeAcquire", std::bind (&TACompletionHandler, mHash, map));
}

void TransactionAcquire::onTimer (bool progress, ScopedLockType& psl)
{
    bool aggressive = false;

    if (getTimeouts () > 10)
    {
        WriteLog (lsWARNING, TransactionAcquire) << "Ten timeouts on TX set " << getHash ();
        psl.unlock();
        {
            Application::ScopedLockType lock (getApp().getMasterLock ());

            if (getApp().getOPs ().stillNeedTXSet (mHash))
            {
                WriteLog (lsWARNING, TransactionAcquire) << "Still need it";
                mTimeouts = 0;
                aggressive = true;
            }
        }
        psl.lock();

        if (!aggressive)
        {
            mFailed = true;
            done ();
            return;
        }
    }

    if (aggressive || !getPeerCount ())
    {
        // out of peers
        WriteLog (lsWARNING, TransactionAcquire) << "Out of peers for TX set " << getHash ();

        bool found = false;
        Overlay::PeerSequence peerList = getApp().overlay ().getActivePeers ();
        BOOST_FOREACH (Peer::ptr const& peer, peerList)
        {
            if (peer->hasTxSet (getHash ()))
            {
                found = true;
                peerHas (peer);
            }
        }

        if (!found)
        {
            BOOST_FOREACH (Peer::ptr const& peer, peerList)
            peerHas (peer);
        }
    }
    else if (!progress)
        trigger (Peer::ptr ());
}

std::weak_ptr<PeerSet> TransactionAcquire::pmDowncast ()
{
    return std::dynamic_pointer_cast<PeerSet> (shared_from_this ());
}

void TransactionAcquire::trigger (Peer::ptr const& peer)
{
    if (mComplete)
    {
        WriteLog (lsINFO, TransactionAcquire) << "trigger after complete";
        return;
    }
    if (mFailed)
    {
        WriteLog (lsINFO, TransactionAcquire) << "trigger after fail";
        return;
    }

    if (!mHaveRoot)
    {
        WriteLog (lsTRACE, TransactionAcquire) << "TransactionAcquire::trigger " << (peer ? "havePeer" : "noPeer") << " no root";
        protocol::TMGetLedger tmGL;
        tmGL.set_ledgerhash (mHash.begin (), mHash.size ());
        tmGL.set_itype (protocol::liTS_CANDIDATE);

        if (getTimeouts () != 0)
            tmGL.set_querytype (protocol::qtINDIRECT);

        * (tmGL.add_nodeids ()) = SHAMapNode ().getRawString ();
        sendRequest (tmGL, peer);
    }
    else if (!mMap->isValid ())
    {
        mFailed = true;
        done ();
    }
    else
    {
        std::vector<SHAMapNode> nodeIDs;
        std::vector<uint256> nodeHashes;
        // VFALCO TODO Use a dependency injection on the temp node cache
        ConsensusTransSetSF sf (getApp().getTempNodeCache ());
        mMap->getMissingNodes (nodeIDs, nodeHashes, 256, &sf);

        if (nodeIDs.empty ())
        {
            if (mMap->isValid ())
                mComplete = true;
            else
                mFailed = true;

            done ();
            return;
        }

        protocol::TMGetLedger tmGL;
        tmGL.set_ledgerhash (mHash.begin (), mHash.size ());
        tmGL.set_itype (protocol::liTS_CANDIDATE);

        if (getTimeouts () != 0)
            tmGL.set_querytype (protocol::qtINDIRECT);

        BOOST_FOREACH (SHAMapNode & it, nodeIDs)
        {
            * (tmGL.add_nodeids ()) = it.getRawString ();
        }
        sendRequest (tmGL, peer);
    }
}

SHAMapAddNode TransactionAcquire::takeNodes (const std::list<SHAMapNode>& nodeIDs,
        const std::list< Blob >& data, Peer::ptr const& peer)
{
    if (mComplete)
    {
        WriteLog (lsTRACE, TransactionAcquire) << "TX set complete";
        return SHAMapAddNode ();
    }

    if (mFailed)
    {
        WriteLog (lsTRACE, TransactionAcquire) << "TX set failed";
        return SHAMapAddNode ();
    }

    try
    {
        if (nodeIDs.empty ())
            return SHAMapAddNode::invalid ();

        std::list<SHAMapNode>::const_iterator nodeIDit = nodeIDs.begin ();
        std::list< Blob >::const_iterator nodeDatait = data.begin ();
        ConsensusTransSetSF sf (getApp().getTempNodeCache ());

        while (nodeIDit != nodeIDs.end ())
        {
            if (nodeIDit->isRoot ())
            {
                if (mHaveRoot)
                    WriteLog (lsDEBUG, TransactionAcquire) << "Got root TXS node, already have it";
                else if (!mMap->addRootNode (getHash (), *nodeDatait, snfWIRE, nullptr).isGood())
                {
                    WriteLog (lsWARNING, TransactionAcquire) << "TX acquire got bad root node";
                }
                else
                    mHaveRoot = true;
            }
            else if (!mMap->addKnownNode (*nodeIDit, *nodeDatait, &sf).isGood())
            {
                WriteLog (lsWARNING, TransactionAcquire) << "TX acquire got bad non-root node";
                return SHAMapAddNode::invalid ();
            }

            ++nodeIDit;
            ++nodeDatait;
        }

        trigger (peer);
        progress ();
        return SHAMapAddNode::useful ();
    }
    catch (...)
    {
        WriteLog (lsERROR, TransactionAcquire) << "Peer sends us junky transaction node data";
        return SHAMapAddNode::invalid ();
    }
}

} // ripple
