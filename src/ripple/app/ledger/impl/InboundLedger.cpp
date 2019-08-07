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

#include <ripple/app/ledger/InboundLedger.h>
#include <ripple/shamap/SHAMapNodeID.h>
#include <ripple/app/ledger/AccountStateSF.h>
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
#include <ripple/protocol/jss.h>
#include <ripple/nodestore/DatabaseShard.h>

#include <algorithm>

namespace ripple {

using namespace std::chrono_literals;

enum
{
    // Number of peers to start with
    peerCountStart = 4

    // Number of peers to add on a timeout
    ,peerCountAdd = 2

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

// millisecond for each ledger timeout
auto constexpr ledgerAcquireTimeout = 2500ms;

InboundLedger::InboundLedger(Application& app, uint256 const& hash,
    std::uint32_t seq, Reason reason, clock_type& clock)
    : PeerSet (app, hash, ledgerAcquireTimeout, clock,
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
    JLOG (m_journal.trace()) << "Acquiring ledger " << mHash;
}

void
InboundLedger::init(ScopedLockType& collectionLock)
{
    ScopedLockType sl (mLock);
    collectionLock.unlock();
    tryDB(app_.family());
    if (mFailed)
        return;
    if (! mComplete)
    {
        auto shardStore = app_.getShardStore();
        if (mReason == Reason::SHARD)
        {
            if (! shardStore || ! app_.shardFamily())
            {
                JLOG(m_journal.error()) <<
                    "Acquiring shard with no shard store available";
                mFailed = true;
                return;
            }
            mHaveHeader = false;
            mHaveTransactions = false;
            mHaveState = false;
            mLedger.reset();
            tryDB(*app_.shardFamily());
            if (mFailed)
                return;
        }
        else if (shardStore && mSeq >= shardStore->earliestSeq())
        {
            if (auto l = shardStore->fetchLedger(mHash, mSeq))
            {
                mHaveHeader = true;
                mHaveTransactions = true;
                mHaveState = true;
                mComplete = true;
                mLedger = std::move(l);
            }
        }
    }
    if (! mComplete)
    {
        addPeers();
        execute();
        return;
    }

    JLOG (m_journal.debug()) <<
        "Acquiring ledger we already have in " <<
        " local store. " << mHash;
    mLedger->setImmutable(app_.config());

    if (mReason == Reason::HISTORY || mReason == Reason::SHARD)
        return;

    app_.getLedgerMaster().storeLedger(mLedger);

    // Check if this could be a newer fully-validated ledger
    if (mReason == Reason::CONSENSUS)
        app_.getLedgerMaster().checkAccept(mLedger);
}

void InboundLedger::execute ()
{
    if (app_.getJobQueue ().getJobCountTotal (jtLEDGER_DATA) > 4)
    {
        JLOG (m_journal.debug()) <<
            "Deferring InboundLedger timer due to load";
        setTimer ();
        return;
    }

    app_.getJobQueue ().addJob (
        jtLEDGER_DATA, "InboundLedger",
        [ptr = shared_from_this()] (Job&)
        {
            ptr->invokeOnTimer ();
        });
}
void InboundLedger::update (std::uint32_t seq)
{
    ScopedLockType sl (mLock);

    // If we didn't know the sequence number, but now do, save it
    if ((seq != 0) && (mSeq == 0))
        mSeq = seq;

    // Prevent this from being swept
    touch ();
}

bool InboundLedger::checkLocal ()
{
    ScopedLockType sl (mLock);
    if (! isDone())
    {
        if (mLedger)
            tryDB(mLedger->stateMap().family());
        else if(mReason == Reason::SHARD)
            tryDB(*app_.shardFamily());
        else
            tryDB(app_.family());
        if (mFailed || mComplete)
        {
            done();
            return true;
        }
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
        JLOG (m_journal.debug()) <<
            "Acquire " << mHash << " abort " <<
            ((getTimeouts () == 0) ? std::string() :
                (std::string ("timeouts:") +
                to_string (getTimeouts ()) + " ")) <<
            mStats.get ();
    }
}

std::vector<uint256>
InboundLedger::neededTxHashes (
    int max, SHAMapSyncFilter* filter) const
{
    std::vector<uint256> ret;

    if (mLedger->info().txHash.isNonZero ())
    {
        if (mLedger->txMap().getHash().isZero ())
            ret.push_back (mLedger->info().txHash);
        else
            ret = mLedger->txMap().getNeededHashes (max, filter);
    }

    return ret;
}

std::vector<uint256>
InboundLedger::neededStateHashes (
    int max, SHAMapSyncFilter* filter) const
{
    std::vector<uint256> ret;

    if (mLedger->info().accountHash.isNonZero ())
    {
        if (mLedger->stateMap().getHash().isZero ())
            ret.push_back (mLedger->info().accountHash);
        else
            ret = mLedger->stateMap().getNeededHashes (max, filter);
    }

    return ret;
}

LedgerInfo
InboundLedger::deserializeHeader (
    Slice data,
    bool hasPrefix)
{
    SerialIter sit (data.data(), data.size());

    if (hasPrefix)
        sit.get32 ();

    LedgerInfo info;

    info.seq = sit.get32 ();
    info.drops = sit.get64 ();
    info.parentHash = sit.get256 ();
    info.txHash = sit.get256 ();
    info.accountHash = sit.get256 ();
    info.parentCloseTime = NetClock::time_point{NetClock::duration{sit.get32()}};
    info.closeTime = NetClock::time_point{NetClock::duration{sit.get32()}};
    info.closeTimeResolution = NetClock::duration{sit.get8()};
    info.closeFlags = sit.get8 ();

    return info;
}

// See how much of the ledger data is stored locally
// Data found in a fetch pack will be stored
void
InboundLedger::tryDB(Family& f)
{
    if (! mHaveHeader)
    {
        auto makeLedger = [&, this](Blob const& data)
            {
                JLOG(m_journal.trace()) <<
                    "Ledger header found in fetch pack";
                mLedger = std::make_shared<Ledger>(
                    deserializeHeader(makeSlice(data), true),
                        app_.config(), f);
                if (mLedger->info().hash != mHash ||
                    (mSeq != 0 && mSeq != mLedger->info().seq))
                {
                    // We know for a fact the ledger can never be acquired
                    JLOG(m_journal.warn()) <<
                        "hash " << mHash <<
                        " seq " << std::to_string(mSeq) <<
                        " cannot be a ledger";
                    mLedger.reset();
                    mFailed = true;
                }
            };

        // Try to fetch the ledger header from the DB
        auto node = f.db().fetch(mHash, mSeq);
        if (! node)
        {
            auto data = app_.getLedgerMaster().getFetchPack(mHash);
            if (! data)
                return;
            JLOG (m_journal.trace()) <<
                "Ledger header found in fetch pack";
            makeLedger(*data);
            if (mLedger)
                f.db().store(hotLEDGER, std::move(*data),
                    mHash, mLedger->info().seq);
        }
        else
        {
            JLOG (m_journal.trace()) <<
                "Ledger header found in node store";
            makeLedger(node->getData());
        }
        if (mFailed)
            return;
        if (mSeq == 0)
            mSeq = mLedger->info().seq;
        mLedger->stateMap().setLedgerSeq(mSeq);
        mLedger->txMap().setLedgerSeq(mSeq);
        mHaveHeader = true;
    }

    if (! mHaveTransactions)
    {
        if (mLedger->info().txHash.isZero())
        {
            JLOG (m_journal.trace()) << "No TXNs to fetch";
            mHaveTransactions = true;
        }
        else
        {
            TransactionStateSF filter(mLedger->txMap().family().db(),
                app_.getLedgerMaster());
            if (mLedger->txMap().fetchRoot(
                SHAMapHash{mLedger->info().txHash}, &filter))
            {
                if (neededTxHashes(1, &filter).empty())
                {
                    JLOG(m_journal.trace()) <<
                        "Had full txn map locally";
                    mHaveTransactions = true;
                }
            }
        }
    }

    if (! mHaveState)
    {
        if (mLedger->info().accountHash.isZero())
        {
            JLOG (m_journal.fatal()) <<
                "We are acquiring a ledger with a zero account hash";
            mFailed = true;
            return;
        }
        AccountStateSF filter(mLedger->stateMap().family().db(),
            app_.getLedgerMaster());
        if (mLedger->stateMap().fetchRoot(
            SHAMapHash{mLedger->info().accountHash}, &filter))
        {
            if (neededStateHashes(1, &filter).empty())
            {
                JLOG(m_journal.trace()) <<
                    "Had full AS map locally";
                mHaveState = true;
            }
        }
    }

    if (mHaveTransactions && mHaveState)
    {
        JLOG(m_journal.debug()) <<
            "Had everything locally";
        mComplete = true;
        mLedger->setImmutable(app_.config());
    }
}

/** Called with a lock by the PeerSet when the timer expires
*/
void InboundLedger::onTimer (bool wasProgress, ScopedLockType&)
{
    mRecentNodes.clear ();

    if (isDone())
    {
        JLOG (m_journal.info()) <<
            "Already done " << mHash;
        return;
    }

    if (getTimeouts () > ledgerTimeoutRetriesMax)
    {
        if (mSeq != 0)
        {
            JLOG (m_journal.warn()) <<
                getTimeouts() << " timeouts for ledger " << mSeq;
        }
        else
        {
            JLOG (m_journal.warn()) <<
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
        JLOG (m_journal.debug()) <<
            "No progress(" << pc <<
            ") for ledger " << mHash;

        // addPeers triggers if the reason is not HISTORY
        // So if the reason IS HISTORY, need to trigger after we add
        // otherwise, we need to trigger before we add
        // so each peer gets triggered once
        if (mReason != Reason::HISTORY)
            trigger (nullptr, TriggerReason::timeout);
        addPeers ();
        if (mReason == Reason::HISTORY)
            trigger (nullptr, TriggerReason::timeout);
    }
}

/** Add more peers to the set, if possible */
void InboundLedger::addPeers ()
{
    app_.overlay().selectPeers (*this,
        (getPeerCount() == 0) ? peerCountStart : peerCountAdd,
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

    JLOG (m_journal.debug()) <<
        "Acquire " << mHash <<
        (mFailed ? " fail " : " ") <<
        ((getTimeouts () == 0) ? std::string() :
            (std::string ("timeouts:") +
            to_string (getTimeouts ()) + " ")) <<
        mStats.get ();

    assert (mComplete || mFailed);

    if (mComplete && ! mFailed && mLedger)
    {
        mLedger->setImmutable (app_.config());
        switch (mReason)
        {
        case Reason::SHARD:
            app_.getShardStore()->setStored(mLedger);
            [[fallthrough]];
        case Reason::HISTORY:
            app_.getInboundLedgers().onLedgerFetched();
            break;
        default:
            app_.getLedgerMaster().storeLedger(mLedger);
            break;
        }
    }

    // We hold the PeerSet lock, so must dispatch
    app_.getJobQueue ().addJob (
        jtLEDGER_DATA, "AcquisitionDone",
        [self = shared_from_this()](Job&)
        {
            if (self->mComplete && !self->mFailed)
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
void InboundLedger::trigger (std::shared_ptr<Peer> const& peer, TriggerReason reason)
{
    ScopedLockType sl (mLock);

    if (isDone ())
    {
        JLOG (m_journal.debug()) <<
            "Trigger on ledger: " << mHash <<
            (mComplete ? " completed" : "") <<
            (mFailed ? " failed" : "");
        return;
    }

    if (auto stream = m_journal.trace())
    {
        if (peer)
            stream <<
                "Trigger acquiring ledger " << mHash << " from " << peer;
        else
            stream <<
                "Trigger acquiring ledger " << mHash;

        if (mComplete || mFailed)
            stream <<
                "complete=" << mComplete << " failed=" << mFailed;
        else
            stream <<
                "header=" << mHaveHeader << " tx=" << mHaveTransactions <<
                    " as=" << mHaveState;
    }

    if (! mHaveHeader)
    {
        tryDB(mReason == Reason::SHARD ?
            *app_.shardFamily() : app_.family());
        if (mFailed)
        {
            JLOG (m_journal.warn()) <<
                " failed local for " << mHash;
            return;
        }
    }

    protocol::TMGetLedger tmGL;
    tmGL.set_ledgerhash (mHash.begin (), mHash.size ());

    if (getTimeouts () != 0)
    { // Be more aggressive if we've timed out at least once
        tmGL.set_querytype (protocol::qtINDIRECT);

        if (! isProgress () && ! mFailed && mByHash &&
            (getTimeouts () > ledgerBecomeAggressiveThreshold))
        {
            auto need = getNeededHashes ();

            if (!need.empty ())
            {
                protocol::TMGetObjectByHash tmBH;
                bool typeSet = false;
                tmBH.set_query (true);
                tmBH.set_ledgerhash (mHash.begin (), mHash.size ());
                for (auto const& p : need)
                {
                    JLOG (m_journal.warn()) <<
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
                        if (mSeq != 0)
                            io->set_ledgerseq(mSeq);
                    }
                }

                auto packet = std::make_shared <Message> (
                    tmBH, protocol::mtGET_OBJECTS);

                for (auto id : mPeers)
                {
                    if (auto p = app_.overlay ().findPeerByShortID (id))
                    {
                        mByHash = false;
                        p->send (packet);
                    }
                }
            }
            else
            {
                JLOG (m_journal.info()) <<
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
        if (mSeq != 0)
            tmGL.set_ledgerseq (mSeq);
        JLOG (m_journal.trace()) <<
            "Sending header request to " <<
             (peer ? "selected peer" : "all peers");
        sendRequest (tmGL, peer);
        return;
    }

    if (mLedger)
        tmGL.set_ledgerseq (mLedger->info().seq);

    if (reason != TriggerReason::reply)
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
            JLOG (m_journal.trace()) <<
                "Sending AS root request to " <<
                (peer ? "selected peer" : "all peers");
            sendRequest (tmGL, peer);
            return;
        }
        else
        {
            AccountStateSF filter(mLedger->stateMap().family().db(),
                app_.getLedgerMaster());

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

                        JLOG (m_journal.trace()) <<
                            "Sending AS node request (" <<
                            nodes.size () << ") to " <<
                            (peer ? "selected peer" : "all peers");
                        sendRequest (tmGL, peer);
                        return;
                    }
                    else
                    {
                        JLOG (m_journal.trace()) <<
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
            JLOG (m_journal.trace()) <<
                "Sending TX root request to " << (
                    peer ? "selected peer" : "all peers");
            sendRequest (tmGL, peer);
            return;
        }
        else
        {
            TransactionStateSF filter(mLedger->txMap().family().db(),
                app_.getLedgerMaster());

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
                    JLOG (m_journal.trace()) <<
                        "Sending TX node request (" <<
                        nodes.size () << ") to " <<
                        (peer ? "selected peer" : "all peers");
                    sendRequest (tmGL, peer);
                    return;
                }
                else
                {
                    JLOG (m_journal.trace()) <<
                        "All TX nodes filtered";
                }
            }
        }
    }

    if (mComplete || mFailed)
    {
        JLOG (m_journal.debug()) <<
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
        JLOG (m_journal.trace()) <<
            "filterNodes: all duplicates";

        if (reason != TriggerReason::timeout)
        {
            nodes.clear ();
            return;
        }
    }
    else
    {
        JLOG (m_journal.trace()) <<
            "filterNodes: pruning duplicates";

        nodes.erase (dup, nodes.end());
    }

    std::size_t const limit = (reason == TriggerReason::reply)
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
    JLOG (m_journal.trace()) <<
        "got header acquiring ledger " << mHash;

    if (mComplete || mFailed || mHaveHeader)
        return true;

    auto* f = mReason == Reason::SHARD ?
        app_.shardFamily() : &app_.family();
    mLedger = std::make_shared<Ledger>(deserializeHeader(
        makeSlice(data), false), app_.config(), *f);
    if (mLedger->info().hash != mHash ||
        (mSeq != 0 && mSeq != mLedger->info().seq))
    {
        JLOG (m_journal.warn()) <<
            "Acquire hash mismatch: " << mLedger->info().hash <<
            "!=" << mHash;
        mLedger.reset ();
        return false;
    }
    if (mSeq == 0)
        mSeq = mLedger->info().seq;
    mLedger->stateMap().setLedgerSeq(mSeq);
    mLedger->txMap().setLedgerSeq(mSeq);
    mHaveHeader = true;

    Serializer s (data.size () + 4);
    s.add32 (HashPrefix::ledgerMaster);
    s.addRaw (data.data(), data.size());
    f->db().store(hotLEDGER, std::move (s.modData ()), mHash, mSeq);

    if (mLedger->info().txHash.isZero ())
        mHaveTransactions = true;

    if (mLedger->info().accountHash.isZero ())
        mHaveState = true;

    mLedger->txMap().setSynching ();
    mLedger->stateMap().setSynching ();

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
        JLOG (m_journal.warn()) <<
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
    TransactionStateSF filter(mLedger->txMap().family().db(),
        app_.getLedgerMaster());

    while (nodeIDit != nodeIDs.cend ())
    {
        if (nodeIDit->isRoot ())
        {
            san += mLedger->txMap().addRootNode (
                SHAMapHash{mLedger->info().txHash},
                    makeSlice(*nodeDatait), snfWIRE, &filter);
            if (!san.isGood())
                return false;
        }
        else
        {
            san +=  mLedger->txMap().addKnownNode (
                *nodeIDit, makeSlice(*nodeDatait), &filter);
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
    JLOG (m_journal.trace()) <<
        "got ASdata (" << nodeIDs.size () <<
        ") acquiring ledger " << mHash;
    if (nodeIDs.size () == 1)
    {
        JLOG(m_journal.trace()) <<
            "got AS node: " << nodeIDs.front ();
    }

    ScopedLockType sl (mLock);

    if (!mHaveHeader)
    {
        JLOG (m_journal.warn()) <<
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
    AccountStateSF filter(mLedger->stateMap().family().db(),
        app_.getLedgerMaster());

    while (nodeIDit != nodeIDs.cend ())
    {
        if (nodeIDit->isRoot ())
        {
            san += mLedger->stateMap().addRootNode (
                SHAMapHash{mLedger->info().accountHash},
                    makeSlice(*nodeDatait), snfWIRE, &filter);
            if (!san.isGood ())
            {
                JLOG (m_journal.warn()) <<
                    "Bad ledger header";
                return false;
            }
        }
        else
        {
            san += mLedger->stateMap().addKnownNode (
                *nodeIDit, makeSlice(*nodeDatait), &filter);
            if (!san.isGood ())
            {
                JLOG (m_journal.warn()) <<
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
bool InboundLedger::takeAsRootNode (Slice const& data, SHAMapAddNode& san)
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

    AccountStateSF filter(mLedger->stateMap().family().db(),
        app_.getLedgerMaster());
    san += mLedger->stateMap().addRootNode (
        SHAMapHash{mLedger->info().accountHash}, data, snfWIRE, &filter);
    return san.isGood();
}

/** Process AS root node received from a peer
    Call with a lock
*/
bool InboundLedger::takeTxRootNode (Slice const& data, SHAMapAddNode& san)
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

    TransactionStateSF filter(mLedger->txMap().family().db(),
        app_.getLedgerMaster());
    san += mLedger->txMap().addRootNode (
        SHAMapHash{mLedger->info().txHash}, data, snfWIRE, &filter);
    return san.isGood();
}

std::vector<InboundLedger::neededHash_t>
InboundLedger::getNeededHashes ()
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
        AccountStateSF filter(mLedger->stateMap().family().db(),
            app_.getLedgerMaster());
        for (auto const& h : neededStateHashes (4, &filter))
        {
            ret.push_back (std::make_pair (
                protocol::TMGetObjectByHash::otSTATE_NODE, h));
        }
    }

    if (!mHaveTransactions)
    {
        TransactionStateSF filter(mLedger->txMap().family().db(),
            app_.getLedgerMaster());
        for (auto const& h : neededTxHashes (4, &filter))
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
bool
InboundLedger::gotData(std::weak_ptr<Peer> peer,
    std::shared_ptr<protocol::TMLedgerData> const& data)
{
    std::lock_guard sl (mReceivedDataLock);

    if (isDone ())
        return false;

    mReceivedData.emplace_back (peer, data);

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
            JLOG (m_journal.warn()) <<
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
                JLOG (m_journal.warn()) <<
                    "Got invalid header data";
                peer->charge (Resource::feeInvalidRequest);
                return -1;
            }
        }


        if (!mHaveState && (packet.nodes ().size () > 1) &&
            !takeAsRootNode (makeSlice(packet.nodes(1).nodedata ()), san))
        {
            JLOG (m_journal.warn()) <<
                "Included AS root invalid";
        }

        if (!mHaveTransactions && (packet.nodes ().size () > 2) &&
            !takeTxRootNode (makeSlice(packet.nodes(2).nodedata ()), san))
        {
            JLOG (m_journal.warn()) <<
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
            JLOG (m_journal.info()) <<
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
                JLOG (m_journal.warn()) <<
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
            JLOG (m_journal.debug()) <<
                "Ledger TX node stats: " << san.get();
        }
        else
        {
            takeAsNode (nodeIDs, nodeData, san);
            JLOG (m_journal.debug()) <<
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

    for (;;)
    {
        data.clear();
        {
            std::lock_guard sl (mReceivedDataLock);

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
    }

    if (chosenPeer)
        trigger (chosenPeer, TriggerReason::reply);
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
        for (auto const& h : neededStateHashes (16, nullptr))
        {
            hv.append (to_string (h));
        }
        ret[jss::needed_state_hashes] = hv;
    }

    if (mHaveHeader && !mHaveTransactions)
    {
        Json::Value hv (Json::arrayValue);
        for (auto const& h : neededTxHashes (16, nullptr))
        {
            hv.append (to_string (h));
        }
        ret[jss::needed_transaction_hashes] = hv;
    }

    return ret;
}

} // ripple
