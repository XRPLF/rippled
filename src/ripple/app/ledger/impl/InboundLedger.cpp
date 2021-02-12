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

#include <ripple/app/ledger/AccountStateSF.h>
#include <ripple/app/ledger/InboundLedger.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/TransactionStateSF.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/Log.h>
#include <ripple/core/JobQueue.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/shamap/SHAMapNodeID.h>

#include <algorithm>

namespace ripple {

using namespace std::chrono_literals;

enum {
    // Number of peers to start with
    peerCountStart = 4

    // Number of peers to add on a timeout
    ,
    peerCountAdd = 2

    // how many timeouts before we give up
    ,
    ledgerTimeoutRetriesMax = 10

    // how many timeouts before we get aggressive
    ,
    ledgerBecomeAggressiveThreshold = 6

    // Number of nodes to find initially
    ,
    missingNodesFind = 256

    // Number of nodes to request for a reply
    ,
    reqNodesReply = 128

    // Number of nodes to request blindly
    ,
    reqNodes = 8
};

// millisecond for each ledger timeout
auto constexpr ledgerAcquireTimeout = 2500ms;

InboundLedger::InboundLedger(
    Application& app,
    uint256 const& hash,
    std::uint32_t seq,
    Reason reason,
    clock_type& clock,
    std::unique_ptr<PeerSet> peerSet)
    : TimeoutCounter(
          app,
          hash,
          ledgerAcquireTimeout,
          {jtLEDGER_DATA, "InboundLedger", 5},
          app.journal("InboundLedger"))
    , m_clock(clock)
    , mHaveHeader(false)
    , mHaveState(false)
    , mHaveTransactions(false)
    , mSignaled(false)
    , mByHash(true)
    , mSeq(seq)
    , mReason(reason)
    , mReceiveDispatched(false)
    , mPeerSet(std::move(peerSet))
{
    JLOG(journal_.trace()) << "Acquiring ledger " << hash_;
    touch();
}

void
InboundLedger::init(ScopedLockType& collectionLock)
{
    ScopedLockType sl(mtx_);
    collectionLock.unlock();

    tryDB(app_.getNodeFamily().db());
    if (failed_)
        return;

    if (!complete_)
    {
        auto shardStore = app_.getShardStore();
        if (mReason == Reason::SHARD)
        {
            if (!shardStore)
            {
                JLOG(journal_.error())
                    << "Acquiring shard with no shard store available";
                failed_ = true;
                return;
            }

            mHaveHeader = false;
            mHaveTransactions = false;
            mHaveState = false;
            mLedger.reset();

            tryDB(app_.getShardFamily()->db());
            if (failed_)
                return;
        }
        else if (shardStore && mSeq >= shardStore->earliestLedgerSeq())
        {
            if (auto l = shardStore->fetchLedger(hash_, mSeq))
            {
                mHaveHeader = true;
                mHaveTransactions = true;
                mHaveState = true;
                complete_ = true;
                mLedger = std::move(l);
            }
        }
    }
    if (!complete_)
    {
        addPeers();
        queueJob(sl);
        return;
    }

    JLOG(journal_.debug()) << "Acquiring ledger we already have in "
                           << " local store. " << hash_;
    mLedger->setImmutable(app_.config());

    if (mReason == Reason::HISTORY || mReason == Reason::SHARD)
        return;

    app_.getLedgerMaster().storeLedger(mLedger);

    // Check if this could be a newer fully-validated ledger
    if (mReason == Reason::CONSENSUS)
        app_.getLedgerMaster().checkAccept(mLedger);
}

std::size_t
InboundLedger::getPeerCount() const
{
    auto const& peerIds = mPeerSet->getPeerIds();
    return std::count_if(peerIds.begin(), peerIds.end(), [this](auto id) {
        return (app_.overlay().findPeerByShortID(id) != nullptr);
    });
}

void
InboundLedger::update(std::uint32_t seq)
{
    ScopedLockType sl(mtx_);

    // If we didn't know the sequence number, but now do, save it
    if ((seq != 0) && (mSeq == 0))
        mSeq = seq;

    // Prevent this from being swept
    touch();
}

bool
InboundLedger::checkLocal()
{
    ScopedLockType sl(mtx_);
    if (!isDone())
    {
        if (mLedger)
            tryDB(mLedger->stateMap().family().db());
        else if (mReason == Reason::SHARD)
            tryDB(app_.getShardFamily()->db());
        else
            tryDB(app_.getNodeFamily().db());
        if (failed_ || complete_)
        {
            done();
            return true;
        }
    }
    return false;
}

InboundLedger::~InboundLedger()
{
    // Save any received AS data not processed. It could be useful
    // for populating a different ledger
    for (auto& entry : mReceivedData)
    {
        if (entry.second->type() == protocol::liAS_NODE)
            app_.getInboundLedgers().gotStaleData(entry.second);
    }
    if (!isDone())
    {
        JLOG(journal_.debug())
            << "Acquire " << hash_ << " abort "
            << ((timeouts_ == 0) ? std::string()
                                 : (std::string("timeouts:") +
                                    std::to_string(timeouts_) + " "))
            << mStats.get();
    }
}

static std::vector<uint256>
neededHashes(
    uint256 const& root,
    SHAMap& map,
    int max,
    SHAMapSyncFilter* filter)
{
    std::vector<uint256> ret;

    if (!root.isZero())
    {
        if (map.getHash().isZero())
            ret.push_back(root);
        else
        {
            auto mn = map.getMissingNodes(max, filter);
            ret.reserve(mn.size());
            for (auto const& n : mn)
                ret.push_back(n.second);
        }
    }

    return ret;
}

std::vector<uint256>
InboundLedger::neededTxHashes(int max, SHAMapSyncFilter* filter) const
{
    return neededHashes(mLedger->info().txHash, mLedger->txMap(), max, filter);
}

std::vector<uint256>
InboundLedger::neededStateHashes(int max, SHAMapSyncFilter* filter) const
{
    return neededHashes(
        mLedger->info().accountHash, mLedger->stateMap(), max, filter);
}

LedgerInfo
deserializeHeader(Slice data, bool hasHash)
{
    SerialIter sit(data.data(), data.size());

    LedgerInfo info;

    info.seq = sit.get32();
    info.drops = sit.get64();
    info.parentHash = sit.get256();
    info.txHash = sit.get256();
    info.accountHash = sit.get256();
    info.parentCloseTime =
        NetClock::time_point{NetClock::duration{sit.get32()}};
    info.closeTime = NetClock::time_point{NetClock::duration{sit.get32()}};
    info.closeTimeResolution = NetClock::duration{sit.get8()};
    info.closeFlags = sit.get8();

    if (hasHash)
        info.hash = sit.get256();

    return info;
}

LedgerInfo
deserializePrefixedHeader(Slice data, bool hasHash)
{
    return deserializeHeader(data + 4, hasHash);
}

// See how much of the ledger data is stored locally
// Data found in a fetch pack will be stored
void
InboundLedger::tryDB(NodeStore::Database& srcDB)
{
    if (!mHaveHeader)
    {
        auto makeLedger = [&, this](Blob const& data) {
            JLOG(journal_.trace()) << "Ledger header found in fetch pack";
            mLedger = std::make_shared<Ledger>(
                deserializePrefixedHeader(makeSlice(data)),
                app_.config(),
                mReason == Reason::SHARD ? *app_.getShardFamily()
                                         : app_.getNodeFamily());
            if (mLedger->info().hash != hash_ ||
                (mSeq != 0 && mSeq != mLedger->info().seq))
            {
                // We know for a fact the ledger can never be acquired
                JLOG(journal_.warn())
                    << "hash " << hash_ << " seq " << std::to_string(mSeq)
                    << " cannot be a ledger";
                mLedger.reset();
                failed_ = true;
            }
        };

        // Try to fetch the ledger header from the DB
        if (auto nodeObject = srcDB.fetchNodeObject(hash_, mSeq))
        {
            JLOG(journal_.trace()) << "Ledger header found in local store";

            makeLedger(nodeObject->getData());
            if (failed_)
                return;

            // Store the ledger header if the source and destination differ
            auto& dstDB{mLedger->stateMap().family().db()};
            if (std::addressof(dstDB) != std::addressof(srcDB))
            {
                Blob blob{nodeObject->getData()};
                dstDB.store(
                    hotLEDGER, std::move(blob), hash_, mLedger->info().seq);
            }
        }
        else
        {
            // Try to fetch the ledger header from a fetch pack
            auto data = app_.getLedgerMaster().getFetchPack(hash_);
            if (!data)
                return;

            JLOG(journal_.trace()) << "Ledger header found in fetch pack";

            makeLedger(*data);
            if (failed_)
                return;

            // Store the ledger header in the ledger's database
            mLedger->stateMap().family().db().store(
                hotLEDGER, std::move(*data), hash_, mLedger->info().seq);
        }

        if (mSeq == 0)
            mSeq = mLedger->info().seq;
        mLedger->stateMap().setLedgerSeq(mSeq);
        mLedger->txMap().setLedgerSeq(mSeq);
        mHaveHeader = true;
    }

    if (!mHaveTransactions)
    {
        if (mLedger->info().txHash.isZero())
        {
            JLOG(journal_.trace()) << "No TXNs to fetch";
            mHaveTransactions = true;
        }
        else
        {
            TransactionStateSF filter(
                mLedger->txMap().family().db(), app_.getLedgerMaster());
            if (mLedger->txMap().fetchRoot(
                    SHAMapHash{mLedger->info().txHash}, &filter))
            {
                if (neededTxHashes(1, &filter).empty())
                {
                    JLOG(journal_.trace()) << "Had full txn map locally";
                    mHaveTransactions = true;
                }
            }
        }
    }

    if (!mHaveState)
    {
        if (mLedger->info().accountHash.isZero())
        {
            JLOG(journal_.fatal())
                << "We are acquiring a ledger with a zero account hash";
            failed_ = true;
            return;
        }
        AccountStateSF filter(
            mLedger->stateMap().family().db(), app_.getLedgerMaster());
        if (mLedger->stateMap().fetchRoot(
                SHAMapHash{mLedger->info().accountHash}, &filter))
        {
            if (neededStateHashes(1, &filter).empty())
            {
                JLOG(journal_.trace()) << "Had full AS map locally";
                mHaveState = true;
            }
        }
    }

    if (mHaveTransactions && mHaveState)
    {
        JLOG(journal_.debug()) << "Had everything locally";
        complete_ = true;
        mLedger->setImmutable(app_.config());
    }
}

/** Called with a lock by the PeerSet when the timer expires
 */
void
InboundLedger::onTimer(bool wasProgress, ScopedLockType&)
{
    mRecentNodes.clear();

    if (isDone())
    {
        JLOG(journal_.info()) << "Already done " << hash_;
        return;
    }

    if (timeouts_ > ledgerTimeoutRetriesMax)
    {
        if (mSeq != 0)
        {
            JLOG(journal_.warn())
                << timeouts_ << " timeouts for ledger " << mSeq;
        }
        else
        {
            JLOG(journal_.warn())
                << timeouts_ << " timeouts for ledger " << hash_;
        }
        failed_ = true;
        done();
        return;
    }

    if (!wasProgress)
    {
        checkLocal();

        mByHash = true;

        std::size_t pc = getPeerCount();
        JLOG(journal_.debug())
            << "No progress(" << pc << ") for ledger " << hash_;

        // addPeers triggers if the reason is not HISTORY
        // So if the reason IS HISTORY, need to trigger after we add
        // otherwise, we need to trigger before we add
        // so each peer gets triggered once
        if (mReason != Reason::HISTORY)
            trigger(nullptr, TriggerReason::timeout);
        addPeers();
        if (mReason == Reason::HISTORY)
            trigger(nullptr, TriggerReason::timeout);
    }
}

/** Add more peers to the set, if possible */
void
InboundLedger::addPeers()
{
    mPeerSet->addPeers(
        (getPeerCount() == 0) ? peerCountStart : peerCountAdd,
        [this](auto peer) { return peer->hasLedger(hash_, mSeq); },
        [this](auto peer) {
            // For historical nodes, do not trigger too soon
            // since a fetch pack is probably coming
            if (mReason != Reason::HISTORY)
                trigger(peer, TriggerReason::added);
        });
}

std::weak_ptr<TimeoutCounter>
InboundLedger::pmDowncast()
{
    return shared_from_this();
}

void
InboundLedger::done()
{
    if (mSignaled)
        return;

    mSignaled = true;
    touch();

    JLOG(journal_.debug()) << "Acquire " << hash_ << (failed_ ? " fail " : " ")
                           << ((timeouts_ == 0)
                                   ? std::string()
                                   : (std::string("timeouts:") +
                                      std::to_string(timeouts_) + " "))
                           << mStats.get();

    assert(complete_ || failed_);

    if (complete_ && !failed_ && mLedger)
    {
        mLedger->setImmutable(app_.config());
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
    app_.getJobQueue().addJob(
        jtLEDGER_DATA, "AcquisitionDone", [self = shared_from_this()](Job&) {
            if (self->complete_ && !self->failed_)
            {
                self->app_.getLedgerMaster().checkAccept(self->getLedger());
                self->app_.getLedgerMaster().tryAdvance();
            }
            else
                self->app_.getInboundLedgers().logFailure(
                    self->hash_, self->mSeq);
        });
}

/** Request more nodes, perhaps from a specific peer
 */
void
InboundLedger::trigger(std::shared_ptr<Peer> const& peer, TriggerReason reason)
{
    ScopedLockType sl(mtx_);

    if (isDone())
    {
        JLOG(journal_.debug())
            << "Trigger on ledger: " << hash_ << (complete_ ? " completed" : "")
            << (failed_ ? " failed" : "");
        return;
    }

    if (auto stream = journal_.trace())
    {
        if (peer)
            stream << "Trigger acquiring ledger " << hash_ << " from " << peer;
        else
            stream << "Trigger acquiring ledger " << hash_;

        if (complete_ || failed_)
            stream << "complete=" << complete_ << " failed=" << failed_;
        else
            stream << "header=" << mHaveHeader << " tx=" << mHaveTransactions
                   << " as=" << mHaveState;
    }

    if (!mHaveHeader)
    {
        tryDB(
            mReason == Reason::SHARD ? app_.getShardFamily()->db()
                                     : app_.getNodeFamily().db());
        if (failed_)
        {
            JLOG(journal_.warn()) << " failed local for " << hash_;
            return;
        }
    }

    protocol::TMGetLedger tmGL;
    tmGL.set_ledgerhash(hash_.begin(), hash_.size());

    if (timeouts_ != 0)
    {
        // Be more aggressive if we've timed out at least once
        tmGL.set_querytype(protocol::qtINDIRECT);

        if (!progress_ && !failed_ && mByHash &&
            (timeouts_ > ledgerBecomeAggressiveThreshold))
        {
            auto need = getNeededHashes();

            if (!need.empty())
            {
                protocol::TMGetObjectByHash tmBH;
                bool typeSet = false;
                tmBH.set_query(true);
                tmBH.set_ledgerhash(hash_.begin(), hash_.size());
                for (auto const& p : need)
                {
                    JLOGV(
                        journal_.warn(),
                        "InboundLedger::trigger want",
                        jv("hash", p.second));

                    if (!typeSet)
                    {
                        tmBH.set_type(p.first);
                        typeSet = true;
                    }

                    if (p.first == tmBH.type())
                    {
                        protocol::TMIndexedObject* io = tmBH.add_objects();
                        io->set_hash(p.second.begin(), p.second.size());
                        if (mSeq != 0)
                            io->set_ledgerseq(mSeq);
                    }
                }

                auto packet =
                    std::make_shared<Message>(tmBH, protocol::mtGET_OBJECTS);
                auto const& peerIds = mPeerSet->getPeerIds();
                std::for_each(
                    peerIds.begin(), peerIds.end(), [this, &packet](auto id) {
                        if (auto p = app_.overlay().findPeerByShortID(id))
                        {
                            mByHash = false;
                            p->send(packet);
                        }
                    });
            }
            else
            {
                JLOG(journal_.info())
                    << "getNeededHashes says acquire is complete";
                mHaveHeader = true;
                mHaveTransactions = true;
                mHaveState = true;
                complete_ = true;
            }
        }
    }

    // We can't do much without the header data because we don't know the
    // state or transaction root hashes.
    if (!mHaveHeader && !failed_)
    {
        tmGL.set_itype(protocol::liBASE);
        if (mSeq != 0)
            tmGL.set_ledgerseq(mSeq);
        JLOG(journal_.trace()) << "Sending header request to "
                               << (peer ? "selected peer" : "all peers");
        mPeerSet->sendRequest(tmGL, peer);
        return;
    }

    if (mLedger)
        tmGL.set_ledgerseq(mLedger->info().seq);

    if (reason != TriggerReason::reply)
    {
        // If we're querying blind, don't query deep
        tmGL.set_querydepth(0);
    }
    else if (peer && peer->isHighLatency())
    {
        // If the peer has high latency, query extra deep
        tmGL.set_querydepth(2);
    }
    else
        tmGL.set_querydepth(1);

    // Get the state data first because it's the most likely to be useful
    // if we wind up abandoning this fetch.
    if (mHaveHeader && !mHaveState && !failed_)
    {
        assert(mLedger);

        if (!mLedger->stateMap().isValid())
        {
            failed_ = true;
        }
        else if (mLedger->stateMap().getHash().isZero())
        {
            // we need the root node
            tmGL.set_itype(protocol::liAS_NODE);
            *tmGL.add_nodeids() = SHAMapNodeID().getRawString();
            JLOG(journal_.trace()) << "Sending AS root request to "
                                   << (peer ? "selected peer" : "all peers");
            mPeerSet->sendRequest(tmGL, peer);
            return;
        }
        else
        {
            AccountStateSF filter(
                mLedger->stateMap().family().db(), app_.getLedgerMaster());

            // Release the lock while we process the large state map
            sl.unlock();
            auto nodes =
                mLedger->stateMap().getMissingNodes(missingNodesFind, &filter);
            sl.lock();

            // Make sure nothing happened while we released the lock
            if (!failed_ && !complete_ && !mHaveState)
            {
                if (nodes.empty())
                {
                    if (!mLedger->stateMap().isValid())
                        failed_ = true;
                    else
                    {
                        mHaveState = true;

                        if (mHaveTransactions)
                            complete_ = true;
                    }
                }
                else
                {
                    filterNodes(nodes, reason);

                    if (!nodes.empty())
                    {
                        tmGL.set_itype(protocol::liAS_NODE);
                        for (auto const& id : nodes)
                        {
                            *(tmGL.add_nodeids()) = id.first.getRawString();
                        }

                        JLOG(journal_.trace())
                            << "Sending AS node request (" << nodes.size()
                            << ") to "
                            << (peer ? "selected peer" : "all peers");
                        mPeerSet->sendRequest(tmGL, peer);
                        return;
                    }
                    else
                    {
                        JLOG(journal_.trace()) << "All AS nodes filtered";
                    }
                }
            }
        }
    }

    if (mHaveHeader && !mHaveTransactions && !failed_)
    {
        assert(mLedger);

        if (!mLedger->txMap().isValid())
        {
            failed_ = true;
        }
        else if (mLedger->txMap().getHash().isZero())
        {
            // we need the root node
            tmGL.set_itype(protocol::liTX_NODE);
            *(tmGL.add_nodeids()) = SHAMapNodeID().getRawString();
            JLOG(journal_.trace()) << "Sending TX root request to "
                                   << (peer ? "selected peer" : "all peers");
            mPeerSet->sendRequest(tmGL, peer);
            return;
        }
        else
        {
            TransactionStateSF filter(
                mLedger->txMap().family().db(), app_.getLedgerMaster());

            auto nodes =
                mLedger->txMap().getMissingNodes(missingNodesFind, &filter);

            if (nodes.empty())
            {
                if (!mLedger->txMap().isValid())
                    failed_ = true;
                else
                {
                    mHaveTransactions = true;

                    if (mHaveState)
                        complete_ = true;
                }
            }
            else
            {
                filterNodes(nodes, reason);

                if (!nodes.empty())
                {
                    tmGL.set_itype(protocol::liTX_NODE);
                    for (auto const& n : nodes)
                    {
                        *(tmGL.add_nodeids()) = n.first.getRawString();
                    }
                    JLOG(journal_.trace())
                        << "Sending TX node request (" << nodes.size()
                        << ") to " << (peer ? "selected peer" : "all peers");
                    mPeerSet->sendRequest(tmGL, peer);
                    return;
                }
                else
                {
                    JLOG(journal_.trace()) << "All TX nodes filtered";
                }
            }
        }
    }

    if (complete_ || failed_)
    {
        JLOG(journal_.debug())
            << "Done:" << (complete_ ? " complete" : "")
            << (failed_ ? " failed " : " ") << mLedger->info().seq;
        sl.unlock();
        done();
    }
}

void
InboundLedger::filterNodes(
    std::vector<std::pair<SHAMapNodeID, uint256>>& nodes,
    TriggerReason reason)
{
    // Sort nodes so that the ones we haven't recently
    // requested come before the ones we have.
    auto dup = std::stable_partition(
        nodes.begin(), nodes.end(), [this](auto const& item) {
            return mRecentNodes.count(item.second) == 0;
        });

    // If everything is a duplicate we don't want to send
    // any query at all except on a timeout where we need
    // to query everyone:
    if (dup == nodes.begin())
    {
        JLOG(journal_.trace()) << "filterNodes: all duplicates";

        if (reason != TriggerReason::timeout)
        {
            nodes.clear();
            return;
        }
    }
    else
    {
        JLOG(journal_.trace()) << "filterNodes: pruning duplicates";

        nodes.erase(dup, nodes.end());
    }

    std::size_t const limit =
        (reason == TriggerReason::reply) ? reqNodesReply : reqNodes;

    if (nodes.size() > limit)
        nodes.resize(limit);

    for (auto const& n : nodes)
        mRecentNodes.insert(n.second);
}

/** Take ledger header data
    Call with a lock
*/
// data must not have hash prefix
bool
InboundLedger::takeHeader(std::string const& data)
{
    // Return value: true=normal, false=bad data
    JLOG(journal_.trace()) << "got header acquiring ledger " << hash_;

    if (complete_ || failed_ || mHaveHeader)
        return true;

    auto* f = mReason == Reason::SHARD ? app_.getShardFamily()
                                       : &app_.getNodeFamily();
    mLedger = std::make_shared<Ledger>(
        deserializeHeader(makeSlice(data)), app_.config(), *f);
    if (mLedger->info().hash != hash_ ||
        (mSeq != 0 && mSeq != mLedger->info().seq))
    {
        JLOG(journal_.warn())
            << "Acquire hash mismatch: " << mLedger->info().hash
            << "!=" << hash_;
        mLedger.reset();
        return false;
    }
    if (mSeq == 0)
        mSeq = mLedger->info().seq;
    mLedger->stateMap().setLedgerSeq(mSeq);
    mLedger->txMap().setLedgerSeq(mSeq);
    mHaveHeader = true;

    Serializer s(data.size() + 4);
    s.add32(HashPrefix::ledgerMaster);
    s.addRaw(data.data(), data.size());
    f->db().store(hotLEDGER, std::move(s.modData()), hash_, mSeq);

    if (mLedger->info().txHash.isZero())
        mHaveTransactions = true;

    if (mLedger->info().accountHash.isZero())
        mHaveState = true;

    mLedger->txMap().setSynching();
    mLedger->stateMap().setSynching();

    return true;
}

/** Process node data received from a peer
    Call with a lock
*/
void
InboundLedger::receiveNode(protocol::TMLedgerData& packet, SHAMapAddNode& san)
{
    if (!mHaveHeader)
    {
        JLOG(journal_.warn()) << "Missing ledger header";
        san.incInvalid();
        return;
    }
    if (packet.type() == protocol::liTX_NODE)
    {
        if (mHaveTransactions || failed_)
        {
            san.incDuplicate();
            return;
        }
    }
    else if (mHaveState || failed_)
    {
        san.incDuplicate();
        return;
    }

    auto [map, rootHash, filter] = [&]()
        -> std::tuple<SHAMap&, SHAMapHash, std::unique_ptr<SHAMapSyncFilter>> {
        if (packet.type() == protocol::liTX_NODE)
            return {
                mLedger->txMap(),
                SHAMapHash{mLedger->info().txHash},
                std::make_unique<TransactionStateSF>(
                    mLedger->txMap().family().db(), app_.getLedgerMaster())};
        return {
            mLedger->stateMap(),
            SHAMapHash{mLedger->info().accountHash},
            std::make_unique<AccountStateSF>(
                mLedger->stateMap().family().db(), app_.getLedgerMaster())};
    }();

    try
    {
        for (auto const& node : packet.nodes())
        {
            auto const nodeID = deserializeSHAMapNodeID(node.nodeid());

            if (!nodeID)
            {
                san.incInvalid();
                return;
            }

            if (nodeID->isRoot())
                san += map.addRootNode(
                    rootHash, makeSlice(node.nodedata()), filter.get());
            else
                san += map.addKnownNode(
                    *nodeID, makeSlice(node.nodedata()), filter.get());

            if (!san.isGood())
            {
                JLOG(journal_.warn()) << "Received bad node data";
                return;
            }
        }
    }
    catch (std::exception const& e)
    {
        JLOG(journal_.error()) << "Received bad node data: " << e.what();
        san.incInvalid();
        return;
    }

    if (!map.isSynching())
    {
        if (packet.type() == protocol::liTX_NODE)
            mHaveTransactions = true;
        else
            mHaveState = true;

        if (mHaveTransactions && mHaveState)
        {
            complete_ = true;
            done();
        }
    }
}

/** Process AS root node received from a peer
    Call with a lock
*/
bool
InboundLedger::takeAsRootNode(Slice const& data, SHAMapAddNode& san)
{
    if (failed_ || mHaveState)
    {
        san.incDuplicate();
        return true;
    }

    if (!mHaveHeader)
    {
        assert(false);
        return false;
    }

    AccountStateSF filter(
        mLedger->stateMap().family().db(), app_.getLedgerMaster());
    san += mLedger->stateMap().addRootNode(
        SHAMapHash{mLedger->info().accountHash}, data, &filter);
    return san.isGood();
}

/** Process AS root node received from a peer
    Call with a lock
*/
bool
InboundLedger::takeTxRootNode(Slice const& data, SHAMapAddNode& san)
{
    if (failed_ || mHaveTransactions)
    {
        san.incDuplicate();
        return true;
    }

    if (!mHaveHeader)
    {
        assert(false);
        return false;
    }

    TransactionStateSF filter(
        mLedger->txMap().family().db(), app_.getLedgerMaster());
    san += mLedger->txMap().addRootNode(
        SHAMapHash{mLedger->info().txHash}, data, &filter);
    return san.isGood();
}

std::vector<InboundLedger::neededHash_t>
InboundLedger::getNeededHashes()
{
    std::vector<neededHash_t> ret;

    if (!mHaveHeader)
    {
        ret.push_back(
            std::make_pair(protocol::TMGetObjectByHash::otLEDGER, hash_));
        return ret;
    }

    if (!mHaveState)
    {
        AccountStateSF filter(
            mLedger->stateMap().family().db(), app_.getLedgerMaster());
        for (auto const& h : neededStateHashes(4, &filter))
        {
            ret.push_back(
                std::make_pair(protocol::TMGetObjectByHash::otSTATE_NODE, h));
        }
    }

    if (!mHaveTransactions)
    {
        TransactionStateSF filter(
            mLedger->txMap().family().db(), app_.getLedgerMaster());
        for (auto const& h : neededTxHashes(4, &filter))
        {
            ret.push_back(std::make_pair(
                protocol::TMGetObjectByHash::otTRANSACTION_NODE, h));
        }
    }

    return ret;
}

/** Stash a TMLedgerData received from a peer for later processing
    Returns 'true' if we need to dispatch
*/
bool
InboundLedger::gotData(
    std::weak_ptr<Peer> peer,
    std::shared_ptr<protocol::TMLedgerData> const& data)
{
    std::lock_guard sl(mReceivedDataLock);

    if (isDone())
        return false;

    mReceivedData.emplace_back(peer, data);

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
int
InboundLedger::processData(
    std::shared_ptr<Peer> peer,
    protocol::TMLedgerData& packet)
{
    ScopedLockType sl(mtx_);

    if (packet.type() == protocol::liBASE)
    {
        if (packet.nodes_size() < 1)
        {
            JLOG(journal_.warn()) << "Got empty header data";
            peer->charge(Resource::feeInvalidRequest);
            return -1;
        }

        SHAMapAddNode san;

        try
        {
            if (!mHaveHeader)
            {
                if (!takeHeader(packet.nodes(0).nodedata()))
                {
                    JLOG(journal_.warn()) << "Got invalid header data";
                    peer->charge(Resource::feeInvalidRequest);
                    return -1;
                }

                san.incUseful();
            }

            if (!mHaveState && (packet.nodes().size() > 1) &&
                !takeAsRootNode(makeSlice(packet.nodes(1).nodedata()), san))
            {
                JLOG(journal_.warn()) << "Included AS root invalid";
            }

            if (!mHaveTransactions && (packet.nodes().size() > 2) &&
                !takeTxRootNode(makeSlice(packet.nodes(2).nodedata()), san))
            {
                JLOG(journal_.warn()) << "Included TX root invalid";
            }
        }
        catch (std::exception const& ex)
        {
            JLOG(journal_.warn())
                << "Included AS/TX root invalid: " << ex.what();
            peer->charge(Resource::feeBadData);
            return -1;
        }

        if (san.isUseful())
            progress_ = true;

        mStats += san;
        return san.getGood();
    }

    if ((packet.type() == protocol::liTX_NODE) ||
        (packet.type() == protocol::liAS_NODE))
    {
        if (packet.nodes().size() == 0)
        {
            JLOG(journal_.info()) << "Got response with no nodes";
            peer->charge(Resource::feeInvalidRequest);
            return -1;
        }

        // Verify node IDs and data are complete
        for (auto const& node : packet.nodes())
        {
            if (!node.has_nodeid() || !node.has_nodedata())
            {
                JLOG(journal_.warn()) << "Got bad node";
                peer->charge(Resource::feeInvalidRequest);
                return -1;
            }
        }

        SHAMapAddNode san;
        receiveNode(packet, san);

        if (packet.type() == protocol::liTX_NODE)
        {
            JLOG(journal_.debug()) << "Ledger TX node stats: " << san.get();
        }
        else
        {
            JLOG(journal_.debug()) << "Ledger AS node stats: " << san.get();
        }

        if (san.isUseful())
            progress_ = true;

        mStats += san;
        return san.getGood();
    }

    return -1;
}

/** Process pending TMLedgerData
    Query the 'best' peer
*/
void
InboundLedger::runData()
{
    std::shared_ptr<Peer> chosenPeer;
    int chosenPeerCount = -1;

    std::vector<PeerDataPairType> data;

    for (;;)
    {
        data.clear();
        {
            std::lock_guard sl(mReceivedDataLock);

            if (mReceivedData.empty())
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
                int count = processData(peer, *(entry.second));
                if (count > chosenPeerCount)
                {
                    chosenPeerCount = count;
                    chosenPeer = std::move(peer);
                }
            }
        }
    }

    if (chosenPeer)
        trigger(chosenPeer, TriggerReason::reply);
}

Json::Value
InboundLedger::getJson(int)
{
    Json::Value ret(Json::objectValue);

    ScopedLockType sl(mtx_);

    ret[jss::hash] = to_string(hash_);

    if (complete_)
        ret[jss::complete] = true;

    if (failed_)
        ret[jss::failed] = true;

    if (!complete_ && !failed_)
        ret[jss::peers] = static_cast<int>(mPeerSet->getPeerIds().size());

    ret[jss::have_header] = mHaveHeader;

    if (mHaveHeader)
    {
        ret[jss::have_state] = mHaveState;
        ret[jss::have_transactions] = mHaveTransactions;
    }

    ret[jss::timeouts] = timeouts_;

    if (mHaveHeader && !mHaveState)
    {
        Json::Value hv(Json::arrayValue);
        for (auto const& h : neededStateHashes(16, nullptr))
        {
            hv.append(to_string(h));
        }
        ret[jss::needed_state_hashes] = hv;
    }

    if (mHaveHeader && !mHaveTransactions)
    {
        Json::Value hv(Json::arrayValue);
        for (auto const& h : neededTxHashes(16, nullptr))
        {
            hv.append(to_string(h));
        }
        ret[jss::needed_transaction_hashes] = hv;
    }

    return ret;
}

}  // namespace ripple
