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

#include <BeastConfig.h>
#include <ripple/app/ledger/ConsensusTransSetSF.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/tx/TransactionAcquire.h>
#include <ripple/app/tx/InboundTransactions.h>
#include <ripple/overlay/Overlay.h>
#include <beast/utility/make_lock.h>
#include <memory>

namespace ripple {

enum
{
    // Timeout interval in milliseconds
    TX_ACQUIRE_TIMEOUT = 250,

    NORM_TIMEOUTS = 4,
    MAX_TIMEOUTS = 20,
};

TransactionAcquire::TransactionAcquire (uint256 const& hash, clock_type& clock)
    : PeerSet (hash, TX_ACQUIRE_TIMEOUT, true, clock,
        deprecatedLogs().journal("TransactionAcquire"))
    , mHaveRoot (false)
{
    mMap = std::make_shared<SHAMap> (SHAMapType::TRANSACTION, hash,
        getApp().family(), deprecatedLogs().journal("SHAMap"));
    mMap->setUnbacked ();
}

TransactionAcquire::~TransactionAcquire ()
{
}

void TransactionAcquire::done ()
{
    // We hold a PeerSet lock and so cannot do real work here

    if (mFailed)
    {
        WriteLog (lsWARNING, TransactionAcquire) << "Failed to acquire TX set " << mHash;
    }
    else
    {
        WriteLog (lsDEBUG, TransactionAcquire) << "Acquired TX set " << mHash;
        mMap->setImmutable ();

        uint256 const& hash (mHash);
        std::shared_ptr <SHAMap> const& map (mMap);
        getApp().getJobQueue().addJob (jtTXN_DATA, "completeAcquire",
            [hash, map](Job&)
            {
                getApp().getInboundTransactions().giveSet (
                    hash, map, true);
            });
    }

}

void TransactionAcquire::onTimer (bool progress, ScopedLockType& psl)
{
    bool aggressive = false;

    if (getTimeouts () >= NORM_TIMEOUTS)
    {
        aggressive = true;

        if (getTimeouts () > MAX_TIMEOUTS)
        {
            mFailed = true;
            done ();
            return;
        }
    }

    if (aggressive)
        trigger (Peer::ptr ());

    addPeers (1);
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
        tmGL.set_querydepth (3); // We probably need the whole thing

        if (getTimeouts () != 0)
            tmGL.set_querytype (protocol::qtINDIRECT);

        * (tmGL.add_nodeids ()) = SHAMapNodeID ().getRawString ();
        sendRequest (tmGL, peer);
    }
    else if (!mMap->isValid ())
    {
        mFailed = true;
        done ();
    }
    else
    {
        std::vector<SHAMapNodeID> nodeIDs;
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

        for (SHAMapNodeID& it : nodeIDs)
        {
            *tmGL.add_nodeids () = it.getRawString ();
        }
        sendRequest (tmGL, peer);
    }
}

SHAMapAddNode TransactionAcquire::takeNodes (const std::list<SHAMapNodeID>& nodeIDs,
        const std::list< Blob >& data, Peer::ptr const& peer)
{
    ScopedLockType sl (mLock);

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

        std::list<SHAMapNodeID>::const_iterator nodeIDit = nodeIDs.begin ();
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

void TransactionAcquire::addPeers (int numPeers)
{
    getApp().overlay().selectPeers (*this, numPeers, ScoreHasTxSet (getHash()));
}

void TransactionAcquire::init (int numPeers)
{
    ScopedLockType sl (mLock);

    addPeers (numPeers);

    setTimer ();
}

void TransactionAcquire::stillNeed ()
{
    ScopedLockType sl (mLock);

    if (mTimeouts > NORM_TIMEOUTS)
        mTimeouts = NORM_TIMEOUTS;
}

} // ripple
