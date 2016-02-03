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
#include <ripple/shamap/SHAMapNodeID.h>
#include <ripple/app/ledger/AccountStateSF.h>
#include <ripple/app/ledger/InboundLedger.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/TransactionStateSF.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/Log.h>
#include <ripple/core/JobQueue.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/resource/Fees.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/nodestore/Database.h>
#include <algorithm>

namespace ripple {

enum
{
    // Number of peers to start with
    peerCountStart = 4

    // Number of peers to add on a timeout
    ,peerCountAdd = 2

    // millisecond for each ledger timeout
    ,ledgerAcquireTimeoutMillis = 2500

    // how many timeouts before we giveup
    ,ledgerTimeoutRetriesMax = 10

    // how many timeouts before we get aggressive
    ,ledgerBecomeAggressiveThreshold = 6

    // Number of nodes to find initially
    ,missingNodesFind = 256

    // Number of nodes to request for a reply
    ,reqNodesReply = 128

    // Number of nodes to request blindly
    ,reqNodes = 8
};

InboundLedger::InboundLedger (
    Application& app, uint256 const& hash, std::uint32_t seq, fcReason reason, clock_type& clock)
    : PeerSet (app, hash, ledgerAcquireTimeoutMillis, false, clock,
        app.journal("InboundLedger"))
    , mHaveHeader (false)
    , mHaveState (false)
    , mHaveTransactions (false)
    , mSignaled (false)
    , mByHash (true)
    , mSeq (seq)
    , mReason (reason)
    , mReceiveDispatched (false)
{

    JLOG (m_journal.trace) <<
        "Acquiring ledger " << mHash;
}

void InboundLedger::update (std::uint32_t seq)
{
    ScopedLockType sl (mLock);

    if ((seq != 0) && (mSeq == 0))
    {
        // If we didn't know the sequence number, but now do, save it
        mSeq = seq;
    }

    // Prevent this from being swept
    touch ();
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
    for (auto& entry : mReceivedData)
    {
        if (entry.second->type () == protocol::liAS_NODE)
            app_.getInboundLedgers().gotStaleData(entry.second);
    }
    if (! isDone())
    {
        JLOG (m_journal.debug) <<
            "Acquire " << mHash << " abort " <<
            ((getTimeouts () == 0) ? std::string() :
                (std::string ("timeouts:") +
                to_string (getTimeouts ()) + " ")) <<
            mStats.get ();
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
    }
    else if (!isFailed ())
    {
        JLOG (m_journal.debug) <<
            "Acquiring ledger we already have locally: " << getHash ();
        mLedger->setClosed ();
        mLedger->setImmutable (app_.config());

        if (mReason != fcHISTORY)
            app_.getLedgerMaster ().storeLedger (mLedger);

        // Check if this could be a newer fully-validated ledger
        if (mReason == fcVALIDATION ||
            mReason == fcCURRENT ||
            mReason == fcCONSENSUS)
        {
            app_.getLedgerMaster ().checkAccept (mLedger);
        }
    }
}

/** See how much of the ledger data, if any, is
    in our node store
*/
bool InboundLedger::tryLocal ()
{
    // return value: true = no more work to do

    if (!mHaveHeader)
    {
        // Nothing we can do without the ledger header
        auto node = app_.getNodeStore ().fetch (mHash);

        if (!node)
        {
            Blob data;

            if (!app_.getLedgerMaster ().getFetchPack (mHash, data))
                return false;

            JLOG (m_journal.trace) <<
                "Ledger header found in fetch pack";
            mLedger = std::make_shared<Ledger> (
                data.data(), data.size(), true,
                app_.config(), app_.family());
            app_.getNodeStore ().store (
                hotLEDGER, std::move (data), mHash);
        }
        else
        {
            mLedger = std::make_shared<Ledger>(
                node->getData().data(), node->getData().size(),
                true, app_.config(), app_.family());
        }

        if (mLedger->getHash () != mHash)
        {
            // We know for a fact the ledger can never be acquired
            JLOG (m_journal.warning) <<
                mHash << " cannot be a ledger";
            mFailed = true;
            return true;
        }

        mHaveHeader = true;
    }

    if (!mHaveTransactions)
    {
        if (mLedger->info().txHash.isZero ())
        {
            JLOG (m_journal.trace) <<
                "No TXNs to fetch";
            mHaveTransactions = true;
        }
        else
        {
            TransactionStateSF filter(app_);

            if (mLedger->txMap().fetchRoot (
                SHAMapHash{mLedger->info().txHash}, &filter))
            {
                auto h (mLedger->getNeededTransactionHashes (1, &filter));

                if (h.empty ())
                {
                    JLOG (m_journal.trace) <<
                        "Had full txn map locally";
                    mHaveTransactions = true;
                }
            }
        }
    }

    if (!mHaveState)
    {
        if (mLedger->info().accountHash.isZero ())
        {
            JLOG (m_journal.fatal) <<
                "We are acquiring a ledger with a zero account hash";
            mFailed = true;
            return true;
        }
        else
        {
            AccountStateSF filter(app_);

            if (mLedger->stateMap().fetchRoot (
                SHAMapHash{mLedger->info().accountHash}, &filter))
            {
                auto h (mLedger->getNeededAccountStateHashes (1, &filter));

                if (h.empty ())
                {
                    JLOG (m_journal.trace) <<
                        "Had full AS map locally";
                    mHaveState = true;
                }
            }
        }
    }

    if (mHaveTransactions && mHaveState)
    {
        JLOG (m_journal.debug) <<
            "Had everything locally";
        mComplete = true;
        mLedger->setClosed ();
        mLedger->setImmutable (app_.config());
    }

    return mComplete;
}

/** Called with a lock by the PeerSet when the timer expires
*/
void InboundLedger::onTimer (bool wasProgress, ScopedLockType&)
{
    mRecentNodes.clear ();

    if (isDone())
    {
        JLOG (m_journal.info) <<
            "Already done " << mHash;
        return;
    }

    if (getTimeouts () > ledgerTimeoutRetriesMax)
    {
        if (mSeq != 0)
        {
            JLOG (m_journal.warning) <<
                getTimeouts() << " timeouts for ledger " << mSeq;
        }
        else
        {
            JLOG (m_journal.warning) <<
                getTimeouts() << " timeouts for ledger " << mHash;
        }
        setFailed ();
        done ();
        return;
    }

    if (!wasProgress)
    {
        checkLocal();

        mByHash = true;

        std::size_t pc = getPeerCount ();
        JLOG (m_journal.debug) <<
            "No progress(" << pc <<
            ") for ledger " << mHash;

        // addPeers triggers if the reason is not fcHISTORY
        // So if the reason IS fcHISTORY, need to trigger after we add
        // otherwise, we need to trigger before we add
        // so each peer gets triggered once
        if (mReason != fcHISTORY)
            trigger (Peer::ptr (), TriggerReason::trTimeout);
        addPeers ();
        if (mReason == fcHISTORY)
            trigger (Peer::ptr (), TriggerReason::trTimeout);
    }
}

/** Add more peers to the set, if possible */
void InboundLedger::addPeers ()
{
    app_.overlay().selectPeers (*this,
        (getPeerCount() > 0) ? peerCountStart : peerCountAdd,
        ScoreHasLedger (getHash(), mSeq));
}

std::weak_ptr<PeerSet> InboundLedger::pmDowncast ()
{
    return std::dynamic_pointer_cast<PeerSet> (shared_from_this ());
}

void InboundLedger::done ()
{
    if (mSignaled)
        return;

    mSignaled = true;
    touch ();

    JLOG (m_journal.debug) <<
        "Acquire " << mHash <<
        (isFailed () ? " fail " : " ") <<
        ((getTimeouts () == 0) ? std::string() :
            (std::string ("timeouts:") +
            to_string (getTimeouts ()) + " ")) <<
        mStats.get ();

    assert (isComplete () || isFailed ());

    if (isComplete () && !isFailed () && mLedger)
    {
        mLedger->setClosed ();
        mLedger->setImmutable (app_.config());
        if (mReason != fcHISTORY)
            app_.getLedgerMaster ().storeLedger (mLedger);
        app_.getInboundLedgers().onLedgerFetched(mReason);
    }

    // We hold the PeerSet lock, so must dispatch
    app_.getJobQueue ().addJob (
        jtLEDGER_DATA, "AcquisitionDone",
        [self = shared_from_this()](Job&)
        {
            if (self->isComplete() && !self->isFailed())
            {
                self->app().getLedgerMaster().checkAccept(
                    self->getLedger());
                self->app().getLedgerMaster().tryAdvance();
            }
            else
                self->app().getInboundLedgers().logFailure (
                    self->getHash(), self->getSeq());
        });
}

/** Request more nodes, perhaps from a specific peer
*/
void InboundLedger::trigger (Peer::ptr const& peer, TriggerReason reason)
{
    ScopedLockType sl (mLock);

    if (isDone ())
    {
        JLOG (m_journal.debug) <<
            "Trigger on ledger: " << mHash <<
            (mComplete ? " completed" : "") <<
            (mFailed ? " failed" : "");
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
                "header=" << mHaveHeader << " tx=" << mHaveTransactions <<
                    " as=" << mHaveState;
    }

    if (!mHaveHeader)
    {
        tryLocal ();

        if (mFailed)
        {
            JLOG (m_journal.warning) <<
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
            auto need = getNeededHashes ();

            if (!need.empty ())
            {
                protocol::TMGetObjectByHash tmBH;
                tmBH.set_query (true);
                tmBH.set_ledgerhash (mHash.begin (), mHash.size ());
                bool typeSet = false;
                for (auto& p : need)
                {
                    JLOG (m_journal.warning) <<
                        "Want: " << p.second;

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
                    for (auto it = mPeers.begin (), end = mPeers .end ();
                         it != end; ++it)
                    {
                        Peer::ptr iPeer (
                            app_.overlay ().findPeerByShortID (it->first));

                        if (iPeer)
                        {
                            mByHash = false;
                            iPeer->send (packet);
                        }
                    }
                }
                JLOG (m_journal.info) <<
                    "Attempting by hash fetch for ledger " << mHash;
            }
            else
            {
                JLOG (m_journal.info) <<
                    "getNeededHashes says acquire is complete";
                mHaveHeader = true;
                mHaveTransactions = true;
                mHaveState = true;
                mComplete = true;
            }
        }
    }

    // We can't do much without the header data because we don't know the
    // state or transaction root hashes.
    if (!mHaveHeader && !mFailed)
    {
        tmGL.set_itype (protocol::liBASE);
        JLOG (m_journal.trace) <<
            "Sending header request to " <<
             (peer ? "selected peer" : "all peers");
        sendRequest (tmGL, peer);
        return;
    }

    if (mLedger)
        tmGL.set_ledgerseq (mLedger->info().seq);

    if (reason != TriggerReason::trReply)
    {
        // If we're querying blind, don't query deep
        tmGL.set_querydepth (0);
    }
    else if (peer && peer->isHighLatency ())
    {
        // If the peer has high latency, query extra deep
        tmGL.set_querydepth (2);
    }
    else
        tmGL.set_querydepth (1);

    // Get the state data first because it's the most likely to be useful
    // if we wind up abandoning this fetch.
    if (mHaveHeader && !mHaveState && !mFailed)
    {
        assert (mLedger);

        if (!mLedger->stateMap().isValid ())
        {
            mFailed = true;
        }
        else if (mLedger->stateMap().getHash ().isZero ())
        {
            // we need the root node
            tmGL.set_itype (protocol::liAS_NODE);
            *tmGL.add_nodeids () = SHAMapNodeID ().getRawString ();
            JLOG (m_journal.trace) <<
                "Sending AS root request to " <<
                (peer ? "selected peer" : "all peers");
            sendRequest (tmGL, peer);
            return;
        }
        else
        {
            AccountStateSF filter(app_);

            // Release the lock while we process the large state map
            sl.unlock();
            auto nodes = mLedger->stateMap().getMissingNodes (
                missingNodesFind, &filter);
            sl.lock();

            // Make sure nothing happened while we released the lock
            if (!mFailed && !mComplete && !mHaveState)
            {
                if (nodes.empty ())
                {
                    if (!mLedger->stateMap().isValid ())
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
                    filterNodes (nodes, reason);

                    if (!nodes.empty ())
                    {
                        tmGL.set_itype (protocol::liAS_NODE);
                        for (auto const& id : nodes)
                        {
                            * (tmGL.add_nodeids ()) = id.first.getRawString ();
                        }

                        JLOG (m_journal.trace) <<
                            "Sending AS node request (" <<
                            nodes.size () << ") to " <<
                            (peer ? "selected peer" : "all peers");
                        sendRequest (tmGL, peer);
                        return;
                    }
                    else
                    {
                        JLOG (m_journal.trace) <<
                            "All AS nodes filtered";
                    }
                }
            }
        }
    }

    if (mHaveHeader && !mHaveTransactions && !mFailed)
    {
        assert (mLedger);

        if (!mLedger->txMap().isValid ())
        {
            mFailed = true;
        }
        else if (mLedger->txMap().getHash ().isZero ())
        {
            // we need the root node
            tmGL.set_itype (protocol::liTX_NODE);
            * (tmGL.add_nodeids ()) = SHAMapNodeID ().getRawString ();
            JLOG (m_journal.trace) <<
                "Sending TX root request to " << (
                    peer ? "selected peer" : "all peers");
            sendRequest (tmGL, peer);
            return;
        }
        else
        {
            TransactionStateSF filter(app_);

            auto nodes = mLedger->txMap().getMissingNodes (
                missingNodesFind, &filter);

            if (nodes.empty ())
            {
                if (!mLedger->txMap().isValid ())
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
                filterNodes (nodes, reason);

                if (!nodes.empty ())
                {
                    tmGL.set_itype (protocol::liTX_NODE);
                    for (auto const& n : nodes)
                    {
                        * (tmGL.add_nodeids ()) = n.first.getRawString ();
                    }
                    JLOG (m_journal.trace) <<
                        "Sending TX node request (" <<
                        nodes.size () << ") to " <<
                        (peer ? "selected peer" : "all peers");
                    sendRequest (tmGL, peer);
                    return;
                }
                else
                {
                    JLOG (m_journal.trace) <<
                        "All TX nodes filtered";
                }
            }
        }
    }

    if (mComplete || mFailed)
    {
        JLOG (m_journal.debug) <<
            "Done:" << (mComplete ? " complete" : "") <<
                (mFailed ? " failed " : " ") <<
            mLedger->info().seq;
        sl.unlock ();
        done ();
    }
}

void InboundLedger::filterNodes (
    std::vector<std::pair<SHAMapNodeID, uint256>>& nodes,
    TriggerReason reason)
{
    // Sort nodes so that the ones we haven't recently
    // requested come before the ones we have.
    auto dup = std::stable_partition (
        nodes.begin(), nodes.end(),
        [this](auto const& item)
        {
            return mRecentNodes.count (item.second) == 0;
        });

    // If everything is a duplicate we don't want to send
    // any query at all except on a timeout where we need
    // to query everyone:
    if (dup == nodes.begin ())
    {
        JLOG (m_journal.trace) <<
            "filterNodes: all duplicates";

        if (reason != TriggerReason::trTimeout)
        {
            nodes.clear ();
            return;
        }
    }
    else
    {
        JLOG (m_journal.trace) <<
            "filterNodes: pruning duplicates";

        nodes.erase (dup, nodes.end());
    }

    std::size_t const limit = (reason == TriggerReason::trReply)
        ? reqNodesReply
        : reqNodes;

    if (nodes.size () > limit)
        nodes.resize (limit);

    for (auto const& n : nodes)
        mRecentNodes.insert (n.second);
}

/** Take ledger header data
    Call with a lock
*/
// data must not have hash prefix
bool InboundLedger::takeHeader (std::string const& data)
{
    // Return value: true=normal, false=bad data
    JLOG (m_journal.trace) <<
        "got header acquiring ledger " << mHash;

    if (mComplete || mFailed || mHaveHeader)
        return true;

    mLedger = std::make_shared<Ledger>(
        data.data(), data.size(), false,
        app_.config(), app_.family());

    if (mLedger->getHash () != mHash)
    {
        JLOG (m_journal.warning) <<
            "Acquire hash mismatch: " << mLedger->getHash () <<
            "!=" << mHash;
        mLedger.reset ();
        return false;
    }

    mHaveHeader = true;

    Serializer s (data.size () + 4);
    s.add32 (HashPrefix::ledgerMaster);
    s.addRaw (data.data(), data.size());
    app_.getNodeStore ().store (
        hotLEDGER, std::move (s.modData ()), mHash);

    if (mLedger->info().txHash.isZero ())
        mHaveTransactions = true;

    if (mLedger->info().accountHash.isZero ())
        mHaveState = true;

    mLedger->setAcquiring ();
    return true;
}

/** Process TX data received from a peer
    Call with a lock
*/
bool InboundLedger::takeTxNode (const std::vector<SHAMapNodeID>& nodeIDs,
    const std::vector< Blob >& data, SHAMapAddNode& san)
{
    if (!mHaveHeader)
    {
        JLOG (m_journal.warning) <<
            "TX node without header";
        san.incInvalid();
        return false;
    }

    if (mHaveTransactions || mFailed)
    {
        san.incDuplicate();
        return true;
    }

    auto nodeIDit = nodeIDs.cbegin ();
    auto nodeDatait = data.begin ();
    TransactionStateSF tFilter(app_);

    while (nodeIDit != nodeIDs.cend ())
    {
        if (nodeIDit->isRoot ())
        {
            san += mLedger->txMap().addRootNode (
                SHAMapHash{mLedger->info().txHash}, *nodeDatait, snfWIRE, &tFilter);
            if (!san.isGood())
                return false;
        }
        else
        {
            san +=  mLedger->txMap().addKnownNode (
                *nodeIDit, *nodeDatait, &tFilter);
            if (!san.isGood())
                return false;
        }

        ++nodeIDit;
        ++nodeDatait;
    }

    if (!mLedger->txMap().isSynching ())
    {
        mHaveTransactions = true;

        if (mHaveState)
        {
            mComplete = true;
            done ();
        }
    }

    return true;
}

/** Process AS data received from a peer
    Call with a lock
*/
bool InboundLedger::takeAsNode (const std::vector<SHAMapNodeID>& nodeIDs,
    const std::vector< Blob >& data, SHAMapAddNode& san)
{
    JLOG (m_journal.trace) <<
        "got ASdata (" << nodeIDs.size () <<
        ") acquiring ledger " << mHash;
    if (nodeIDs.size () == 1 && m_journal.trace) m_journal.trace <<
        "got AS node: " << nodeIDs.front ();

    ScopedLockType sl (mLock);

    if (!mHaveHeader)
    {
        JLOG (m_journal.warning) <<
            "Don't have ledger header";
        san.incInvalid();
        return false;
    }

    if (mHaveState || mFailed)
    {
        san.incDuplicate();
        return true;
    }

    auto nodeIDit = nodeIDs.cbegin ();
    auto nodeDatait = data.begin ();
    AccountStateSF tFilter(app_);

    while (nodeIDit != nodeIDs.cend ())
    {
        if (nodeIDit->isRoot ())
        {
            san += mLedger->stateMap().addRootNode (
                SHAMapHash{mLedger->info().accountHash}, *nodeDatait, snfWIRE, &tFilter);
            if (!san.isGood ())
            {
                JLOG (m_journal.warning) <<
                    "Bad ledger header";
                return false;
            }
        }
        else
        {
            san += mLedger->stateMap().addKnownNode (
                *nodeIDit, *nodeDatait, &tFilter);
            if (!san.isGood ())
            {
                JLOG (m_journal.warning) <<
                    "Unable to add AS node";
                return false;
            }
        }

        ++nodeIDit;
        ++nodeDatait;
    }

    if (!mLedger->stateMap().isSynching ())
    {
        mHaveState = true;

        if (mHaveTransactions)
        {
            mComplete = true;
            done ();
        }
    }

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

    if (!mHaveHeader)
    {
        assert(false);
        return false;
    }

    AccountStateSF tFilter(app_);
    san += mLedger->stateMap().addRootNode (
        SHAMapHash{mLedger->info().accountHash}, data, snfWIRE, &tFilter);
    return san.isGood();
}

/** Process AS root node received from a peer
    Call with a lock
*/
bool InboundLedger::takeTxRootNode (Blob const& data, SHAMapAddNode& san)
{
    if (mFailed || mHaveTransactions)
    {
        san.incDuplicate();
        return true;
    }

    if (!mHaveHeader)
    {
        assert(false);
        return false;
    }

    TransactionStateSF tFilter(app_);
    san += mLedger->txMap().addRootNode (
        SHAMapHash{mLedger->info().txHash}, data, snfWIRE, &tFilter);
    return san.isGood();
}

std::vector<InboundLedger::neededHash_t> InboundLedger::getNeededHashes ()
{
    std::vector<neededHash_t> ret;

    if (!mHaveHeader)
    {
        ret.push_back (std::make_pair (
            protocol::TMGetObjectByHash::otLEDGER, mHash));
        return ret;
    }

    if (!mHaveState)
    {
        AccountStateSF filter(app_);
        // VFALCO NOTE What's the number 4?
        for (auto const& h : mLedger->getNeededAccountStateHashes (4, &filter))
        {
            ret.push_back (std::make_pair (
                protocol::TMGetObjectByHash::otSTATE_NODE, h));
        }
    }

    if (!mHaveTransactions)
    {
        TransactionStateSF filter(app_);
        // VFALCO NOTE What's the number 4?
        for (auto const& h : mLedger->getNeededTransactionHashes (4, &filter))
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

    if (isDone ())
        return false;

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
            JLOG (m_journal.warning) <<
                "Got empty header data";
            peer->charge (Resource::feeInvalidRequest);
            return -1;
        }

        SHAMapAddNode san;

        if (!mHaveHeader)
        {
            if (takeHeader (packet.nodes (0).nodedata ()))
                san.incUseful ();
            else
            {
                JLOG (m_journal.warning) <<
                    "Got invalid header data";
                peer->charge (Resource::feeInvalidRequest);
                return -1;
            }
        }


        if (!mHaveState && (packet.nodes ().size () > 1) &&
            !takeAsRootNode (strCopy (packet.nodes (1).nodedata ()), san))
        {
            JLOG (m_journal.warning) <<
                "Included AS root invalid";
        }

        if (!mHaveTransactions && (packet.nodes ().size () > 2) &&
            !takeTxRootNode (strCopy (packet.nodes (2).nodedata ()), san))
        {
            JLOG (m_journal.warning) <<
                "Included TX root invalid";
        }

        if (san.isUseful ())
            progress ();

        mStats += san;
        return san.getGood ();
    }

    if ((packet.type () == protocol::liTX_NODE) || (
        packet.type () == protocol::liAS_NODE))
    {
        if (packet.nodes ().size () == 0)
        {
            JLOG (m_journal.info) <<
                "Got response with no nodes";
            peer->charge (Resource::feeInvalidRequest);
            return -1;
        }

        std::vector<SHAMapNodeID> nodeIDs;
        nodeIDs.reserve(packet.nodes().size());
        std::vector< Blob > nodeData;
        nodeData.reserve(packet.nodes().size());

        for (int i = 0; i < packet.nodes ().size (); ++i)
        {
            const protocol::TMLedgerNode& node = packet.nodes (i);

            if (!node.has_nodeid () || !node.has_nodedata ())
            {
                JLOG (m_journal.warning) <<
                    "Got bad node";
                peer->charge (Resource::feeInvalidRequest);
                return -1;
            }

            nodeIDs.push_back (SHAMapNodeID (node.nodeid ().data (),
                node.nodeid ().size ()));
            nodeData.push_back (Blob (node.nodedata ().begin (),
                node.nodedata ().end ()));
        }

        SHAMapAddNode san;

        if (packet.type () == protocol::liTX_NODE)
        {
            takeTxNode (nodeIDs, nodeData, san);
            JLOG (m_journal.debug) <<
                "Ledger TX node stats: " << san.get();
        }
        else
        {
            takeAsNode (nodeIDs, nodeData, san);
            JLOG (m_journal.debug) <<
                "Ledger AS node stats: " << san.get();
        }

        if (san.isUseful ())
            progress ();

        mStats += san;
        return san.getGood ();
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
        for (auto& entry : data)
        {
            if (auto peer = entry.first.lock())
            {
                int count = processData (peer, *(entry.second));
                if (count > chosenPeerCount)
                {
                    chosenPeerCount = count;
                    chosenPeer = std::move (peer);
                }
            }
        }

    } while (1);

    if (chosenPeer)
        trigger (chosenPeer, TriggerReason::trReply);
}

Json::Value InboundLedger::getJson (int)
{
    Json::Value ret (Json::objectValue);

    ScopedLockType sl (mLock);

    ret[jss::hash] = to_string (mHash);

    if (mComplete)
        ret[jss::complete] = true;

    if (mFailed)
        ret[jss::failed] = true;

    if (!mComplete && !mFailed)
        ret[jss::peers] = static_cast<int>(mPeers.size());

    ret[jss::have_header] = mHaveHeader;

    if (mHaveHeader)
    {
        ret[jss::have_state] = mHaveState;
        ret[jss::have_transactions] = mHaveTransactions;
    }

    ret[jss::timeouts] = getTimeouts ();

    if (mHaveHeader && !mHaveState)
    {
        Json::Value hv (Json::arrayValue);

        // VFALCO Why 16?
        auto v = mLedger->getNeededAccountStateHashes (16, nullptr);
        for (auto const& h : v)
        {
            hv.append (to_string (h));
        }
        ret[jss::needed_state_hashes] = hv;
    }

    if (mHaveHeader && !mHaveTransactions)
    {
        Json::Value hv (Json::arrayValue);
        // VFALCO Why 16?
        auto v = mLedger->getNeededTransactionHashes (16, nullptr);
        for (auto const& h : v)
        {
            hv.append (to_string (h));
        }
        ret[jss::needed_transaction_hashes] = hv;
    }

    return ret;
}

} // ripple
