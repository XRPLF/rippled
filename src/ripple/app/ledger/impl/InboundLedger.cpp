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

#include <boost/iterator/function_output_iterator.hpp>

#include <algorithm>
#include <random>

namespace ripple {

using namespace std::chrono_literals;

enum {
    // Number of peers to start with
    peerCountStart = 5

    // Number of peers to add on a timeout
    ,
    peerCountAdd = 3

    // how many timeouts before we give up
    ,
    ledgerTimeoutRetriesMax = 6

    // how many timeouts before we get aggressive
    ,
    ledgerBecomeAggressiveThreshold = 4

    // Number of nodes to find initially
    ,
    missingNodesFind = 256

    // Number of nodes to request for a reply
    ,
    reqNodesReply = 128

    // Number of nodes to request blindly
    ,
    reqNodes = 12
};

// millisecond for each ledger timeout
auto constexpr ledgerAcquireTimeout = 3000ms;

static constexpr std::uint8_t const IL_BY_HASH = 0x01;
static constexpr std::uint8_t const IL_SIGNALED = 0x02;
static constexpr std::uint8_t const IL_RECEIVE_DISPATCHED = 0x04;
static constexpr std::uint8_t const IL_HAVE_HEADER = 0x08;
static constexpr std::uint8_t const IL_HAVE_STATE = 0x10;
static constexpr std::uint8_t const IL_HAVE_TXNS = 0x20;

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
          {jtLEDGER_DATA, 5, "InboundLedger"},
          IL_BY_HASH,
          app.journal("InboundLedger"))
    , m_clock(clock)
    , mSeq(seq)
    , reason_(reason)
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

    if (hasFailed())
        return;

    if (!hasCompleted())
    {
        auto shardStore = app_.getShardStore();
        if (reason_ == Reason::SHARD)
        {
            if (!shardStore)
            {
                JLOG(journal_.error())
                    << "Acquiring shard with no shard store available";
                markFailed();
                return;
            }

            userdata_ &= ~(IL_HAVE_HEADER | IL_HAVE_STATE | IL_HAVE_TXNS);
            ledger_.reset();

            tryDB(app_.getShardFamily()->db());

            if (hasFailed())
                return;
        }
        else if (shardStore && mSeq >= shardStore->earliestLedgerSeq())
        {
            if (auto l = shardStore->fetchLedger(hash_, mSeq))
            {
                userdata_ |= (IL_HAVE_HEADER | IL_HAVE_STATE | IL_HAVE_TXNS);
                markComplete();
                ledger_ = std::move(l);
            }
        }
    }

    if (!hasCompleted())
    {
        addPeers();
        queueJob(sl);
        return;
    }

    JLOG(journal_.debug()) << "Acquiring ledger we already have in "
                           << " local store. " << hash_;
    assert(
        ledger_->info().seq < XRP_LEDGER_EARLIEST_FEES ||
        ledger_->read(keylet::fees()));
    ledger_->setImmutable();

    if (reason_ == Reason::HISTORY || reason_ == Reason::SHARD)
        return;

    app_.getLedgerMaster().storeLedger(ledger_);

    // Check if this could be a newer fully-validated ledger
    if (reason_ == Reason::CONSENSUS)
        app_.getLedgerMaster().checkAccept(ledger_);
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
        if (ledger_)
            tryDB(ledger_->stateMap().family().db());
        else if (reason_ == Reason::SHARD)
            tryDB(app_.getShardFamily()->db());
        else
            tryDB(app_.getNodeFamily().db());
        if (hasFailed() || hasCompleted())
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
            << "Acquire " << hash_ << " aborted. Timeouts: " << timeouts_
            << ", stats: " << mStats.get();
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
    return neededHashes(ledger_->info().txHash, ledger_->txMap(), max, filter);
}

std::vector<uint256>
InboundLedger::neededStateHashes(int max, SHAMapSyncFilter* filter) const
{
    return neededHashes(
        ledger_->info().accountHash, ledger_->stateMap(), max, filter);
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
    if (!(userdata_ & IL_HAVE_HEADER))
    {
        auto makeLedger = [&, this](Blob const& data) {
            JLOG(journal_.trace()) << "Ledger header found in fetch pack";
            ledger_ = std::make_shared<Ledger>(
                deserializePrefixedHeader(makeSlice(data)),
                app_.config(),
                reason_ == Reason::SHARD ? *app_.getShardFamily()
                                         : app_.getNodeFamily());
            if (ledger_->info().hash != hash_ ||
                (mSeq != 0 && mSeq != ledger_->info().seq))
            {
                // We know for a fact the ledger can never be acquired
                JLOG(journal_.warn())
                    << "hash " << hash_ << " seq " << std::to_string(mSeq)
                    << " cannot be a ledger";
                ledger_.reset();
                markFailed();
            }
        };

        // Try to fetch the ledger header from the DB
        if (auto nodeObject = srcDB.fetchNodeObject(hash_, mSeq))
        {
            JLOG(journal_.trace()) << "Ledger header found in local store";

            makeLedger(nodeObject->getData());
            if (hasFailed())
                return;

            // Store the ledger header if the source and destination differ
            auto& dstDB{ledger_->stateMap().family().db()};
            if (std::addressof(dstDB) != std::addressof(srcDB))
            {
                Blob blob{nodeObject->getData()};
                dstDB.store(
                    hotLEDGER, std::move(blob), hash_, ledger_->info().seq);
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
            if (hasFailed())
                return;

            // Store the ledger header in the ledger's database
            ledger_->stateMap().family().db().store(
                hotLEDGER, std::move(*data), hash_, ledger_->info().seq);
        }

        if (mSeq == 0)
            mSeq = ledger_->info().seq;
        ledger_->stateMap().setLedgerSeq(mSeq);
        ledger_->txMap().setLedgerSeq(mSeq);
        userdata_ |= IL_HAVE_HEADER;
    }

    if (!(userdata_ & IL_HAVE_TXNS))
    {
        if (ledger_->info().txHash.isZero())
        {
            JLOG(journal_.trace()) << "No TXNs to fetch";
            userdata_ |= IL_HAVE_TXNS;
        }
        else
        {
            TransactionStateSF filter(
                ledger_->txMap().family().db(), app_.getLedgerMaster());
            if (ledger_->txMap().fetchRoot(
                    SHAMapHash{ledger_->info().txHash}, &filter))
            {
                if (neededTxHashes(1, &filter).empty())
                {
                    JLOG(journal_.trace()) << "Had full txn map locally";
                    userdata_ |= IL_HAVE_TXNS;
                }
            }
        }
    }

    if (!(userdata_ & IL_HAVE_STATE))
    {
        if (ledger_->info().accountHash.isZero())
        {
            JLOG(journal_.fatal())
                << "We are acquiring a ledger with a zero account hash";
            markFailed();
            return;
        }
        AccountStateSF filter(
            ledger_->stateMap().family().db(), app_.getLedgerMaster());
        if (ledger_->stateMap().fetchRoot(
                SHAMapHash{ledger_->info().accountHash}, &filter))
        {
            if (neededStateHashes(1, &filter).empty())
            {
                JLOG(journal_.trace()) << "Had full AS map locally";
                userdata_ |= IL_HAVE_STATE;
            }
        }
    }

    if ((IL_HAVE_TXNS | IL_HAVE_STATE) ==
        (userdata_ & (IL_HAVE_TXNS | IL_HAVE_STATE)))
    {
        JLOG(journal_.debug()) << "Had everything locally";
        markComplete();
        assert(
            ledger_->info().seq < XRP_LEDGER_EARLIEST_FEES ||
            ledger_->read(keylet::fees()));
        ledger_->setImmutable();
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
        markFailed();
        done();
        return;
    }

    if (!wasProgress)
    {
        checkLocal();

        userdata_ |= IL_BY_HASH;

        JLOG(journal_.debug())
            << "No progress(" << getPeerCount() << ") for ledger " << hash_;

        // addPeers triggers if the reason is not HISTORY
        // So if the reason IS HISTORY, need to trigger after we add
        // otherwise, we need to trigger before we add
        // so each peer gets triggered once
        if (reason_ != Reason::HISTORY)
            trigger(nullptr, TriggerReason::timeout);
        addPeers();
        if (reason_ == Reason::HISTORY)
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
            if (reason_ != Reason::HISTORY)
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
    // Nothing to do if it was already signaled.
    if (userdata_.fetch_or(IL_SIGNALED) & IL_SIGNALED)
        return;

    touch();

    JLOG(journal_.debug()) << "Acquire " << hash_
                           << (hasFailed() ? " fail " : " ")
                           << ((timeouts_ == 0)
                                   ? std::string()
                                   : (std::string("timeouts:") +
                                      std::to_string(timeouts_) + " "))
                           << mStats.get();

    assert(hasCompleted() || hasFailed());

    if (hasCompleted() && !hasFailed() && ledger_)
    {
        assert(
            ledger_->info().seq < XRP_LEDGER_EARLIEST_FEES ||
            ledger_->read(keylet::fees()));
        ledger_->setImmutable();
        switch (reason_)
        {
            case Reason::SHARD:
                app_.getShardStore()->setStored(ledger_);
                [[fallthrough]];
            case Reason::HISTORY:
                app_.getInboundLedgers().onLedgerFetched();
                break;
            default:
                app_.getLedgerMaster().storeLedger(ledger_);
                break;
        }
    }

    // We hold the PeerSet lock, so must dispatch
    app_.getJobQueue().addJob(
        jtLEDGER_DATA, "AcquisitionDone", [self = shared_from_this()]() {
            if (self->hasCompleted() && !self->hasFailed())
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
        JLOG(journal_.debug()) << "Trigger on ledger: " << hash_
                               << (hasCompleted() ? " completed" : "")
                               << (hasFailed() ? " failed" : "");
        return;
    }

    if (auto stream = journal_.trace())
    {
        if (peer)
            stream << "Trigger acquiring ledger " << hash_ << " from " << peer;
        else
            stream << "Trigger acquiring ledger " << hash_;

        if (hasCompleted() || hasFailed())
            stream << "complete=" << hasCompleted()
                   << " failed=" << hasFailed();
        else
        {
            auto [haveHeader, haveState, haveTransactions] =
                [](std::uint8_t flags) {
                    return std::make_tuple(
                        (flags & IL_HAVE_HEADER) == IL_HAVE_HEADER,
                        (flags & IL_HAVE_STATE) == IL_HAVE_STATE,
                        (flags & IL_HAVE_TXNS) == IL_HAVE_TXNS);
                }(userdata_);

            stream << "header=" << haveHeader << " tx=" << haveTransactions
                   << " as=" << haveState;
        }
    }

    if (!(userdata_ & IL_HAVE_HEADER))
    {
        tryDB(
            reason_ == Reason::SHARD ? app_.getShardFamily()->db()
                                     : app_.getNodeFamily().db());
        if (hasFailed())
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

        if (!hasProgressed() && !hasFailed() && (userdata_ & IL_BY_HASH) &&
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
                    JLOG(journal_.debug()) << "Want: " << p.second;

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
                            userdata_ &= ~IL_BY_HASH;
                            p->send(packet);
                        }
                    });
            }
            else
            {
                JLOG(journal_.info())
                    << "getNeededHashes says acquire is complete";
                userdata_ |= (IL_HAVE_HEADER | IL_HAVE_STATE | IL_HAVE_TXNS);
                markComplete();
            }
        }
    }

    // We can't do much without the header data because we don't know the
    // state or transaction root hashes.
    if (!(userdata_ & IL_HAVE_HEADER) && !hasFailed())
    {
        tmGL.set_itype(protocol::liBASE);
        if (mSeq != 0)
            tmGL.set_ledgerseq(mSeq);
        JLOG(journal_.trace()) << "Sending header request to "
                               << (peer ? "selected peer" : "all peers");
        mPeerSet->sendRequest(tmGL, peer);
        return;
    }

    if (ledger_)
        tmGL.set_ledgerseq(ledger_->info().seq);

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
    if (((userdata_ & (IL_HAVE_HEADER | IL_HAVE_STATE)) == IL_HAVE_HEADER) &&
        !hasFailed())
    {
        assert(ledger_);

        if (!ledger_->stateMap().isValid())
        {
            markFailed();
        }
        else if (ledger_->stateMap().getHash().isZero())
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
                ledger_->stateMap().family().db(), app_.getLedgerMaster());

            // Release the lock while we process the large state map
            sl.unlock();
            auto nodes =
                ledger_->stateMap().getMissingNodes(missingNodesFind, &filter);
            sl.lock();

            // Make sure nothing happened while we released the lock
            if (!hasFailed() && !hasCompleted() && !(userdata_ & IL_HAVE_STATE))
            {
                if (nodes.empty())
                {
                    if (!ledger_->stateMap().isValid())
                        markFailed();
                    else
                    {
                        userdata_ |= IL_HAVE_STATE;

                        if (userdata_ & IL_HAVE_TXNS)
                            markComplete();
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

    if (((userdata_ & (IL_HAVE_HEADER | IL_HAVE_TXNS)) == IL_HAVE_HEADER) &&
        !hasFailed())
    {
        assert(ledger_);

        if (!ledger_->txMap().isValid())
        {
            markFailed();
        }
        else if (ledger_->txMap().getHash().isZero())
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
                ledger_->txMap().family().db(), app_.getLedgerMaster());

            auto nodes =
                ledger_->txMap().getMissingNodes(missingNodesFind, &filter);

            if (nodes.empty())
            {
                if (!ledger_->txMap().isValid())
                    markFailed();
                else
                {
                    userdata_ |= IL_HAVE_TXNS;

                    if (userdata_ & IL_HAVE_STATE)
                        markComplete();
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

    if (hasCompleted() || hasFailed())
    {
        JLOG(journal_.debug())
            << "Done:" << (hasCompleted() ? " complete" : "")
            << (hasFailed() ? " failed " : " ") << ledger_->info().seq;
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

    if (hasCompleted() || hasFailed() || (userdata_ & IL_HAVE_HEADER))
        return true;

    auto* f = reason_ == Reason::SHARD ? app_.getShardFamily()
                                       : &app_.getNodeFamily();
    ledger_ = std::make_shared<Ledger>(
        deserializeHeader(makeSlice(data)), app_.config(), *f);
    if (ledger_->info().hash != hash_ ||
        (mSeq != 0 && mSeq != ledger_->info().seq))
    {
        JLOG(journal_.warn())
            << "Acquire hash mismatch: " << ledger_->info().hash
            << "!=" << hash_;
        ledger_.reset();
        return false;
    }
    if (mSeq == 0)
        mSeq = ledger_->info().seq;
    ledger_->stateMap().setLedgerSeq(mSeq);
    ledger_->txMap().setLedgerSeq(mSeq);
    userdata_ |= IL_HAVE_HEADER;

    Serializer s(data.size() + 4);
    s.add32(HashPrefix::ledgerMaster);
    s.addRaw(data.data(), data.size());
    f->db().store(hotLEDGER, std::move(s.modData()), hash_, mSeq);

    if (ledger_->info().txHash.isZero())
        userdata_ |= IL_HAVE_TXNS;

    if (ledger_->info().accountHash.isZero())
        userdata_ |= IL_HAVE_STATE;

    ledger_->txMap().setSynching();
    ledger_->stateMap().setSynching();

    return true;
}

/** Process node data received from a peer
    Call with a lock
*/
void
InboundLedger::receiveNode(protocol::TMLedgerData& packet, SHAMapAddNode& san)
{
    if (!(userdata_ & IL_HAVE_HEADER))
    {
        JLOG(journal_.warn()) << "Missing ledger header";
        san.incInvalid();
        return;
    }
    if (packet.type() == protocol::liTX_NODE)
    {
        if ((userdata_ & IL_HAVE_TXNS) || hasFailed())
        {
            san.incDuplicate();
            return;
        }
    }
    else if ((userdata_ & IL_HAVE_STATE) || hasFailed())
    {
        san.incDuplicate();
        return;
    }

    auto [map, rootHash, filter] = [&]()
        -> std::tuple<SHAMap&, SHAMapHash, std::unique_ptr<SHAMapSyncFilter>> {
        if (packet.type() == protocol::liTX_NODE)
            return {
                ledger_->txMap(),
                SHAMapHash{ledger_->info().txHash},
                std::make_unique<TransactionStateSF>(
                    ledger_->txMap().family().db(), app_.getLedgerMaster())};
        return {
            ledger_->stateMap(),
            SHAMapHash{ledger_->info().accountHash},
            std::make_unique<AccountStateSF>(
                ledger_->stateMap().family().db(), app_.getLedgerMaster())};
    }();

    try
    {
        auto const f = filter.get();

        for (auto const& node : packet.nodes())
        {
            auto const nodeID = deserializeSHAMapNodeID(node.nodeid());

            if (!nodeID)
                throw std::runtime_error("data does not properly deserialize");

            if (nodeID->isRoot())
            {
                san += map.addRootNode(rootHash, makeSlice(node.nodedata()), f);
            }
            else
            {
                san += map.addKnownNode(*nodeID, makeSlice(node.nodedata()), f);
            }

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
            userdata_ |= IL_HAVE_TXNS;
        else
            userdata_ |= IL_HAVE_STATE;

        if ((IL_HAVE_STATE | IL_HAVE_TXNS) ==
            (userdata_ & (IL_HAVE_STATE | IL_HAVE_TXNS)))
        {
            markComplete();
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
    if (hasFailed() || (userdata_ & IL_HAVE_STATE))
    {
        san.incDuplicate();
        return true;
    }

    if (!(userdata_ & IL_HAVE_HEADER))
    {
        assert(false);
        return false;
    }

    AccountStateSF filter(
        ledger_->stateMap().family().db(), app_.getLedgerMaster());
    san += ledger_->stateMap().addRootNode(
        SHAMapHash{ledger_->info().accountHash}, data, &filter);
    return san.isGood();
}

/** Process AS root node received from a peer
    Call with a lock
*/
bool
InboundLedger::takeTxRootNode(Slice const& data, SHAMapAddNode& san)
{
    if (hasFailed() || (userdata_ & IL_HAVE_TXNS))
    {
        san.incDuplicate();
        return true;
    }

    if (!(userdata_ & IL_HAVE_HEADER))
    {
        assert(false);
        return false;
    }

    TransactionStateSF filter(
        ledger_->txMap().family().db(), app_.getLedgerMaster());
    san += ledger_->txMap().addRootNode(
        SHAMapHash{ledger_->info().txHash}, data, &filter);
    return san.isGood();
}

std::vector<InboundLedger::neededHash_t>
InboundLedger::getNeededHashes()
{
    std::vector<neededHash_t> ret;

    if (!(userdata_ & IL_HAVE_HEADER))
    {
        ret.push_back(
            std::make_pair(protocol::TMGetObjectByHash::otLEDGER, hash_));
        return ret;
    }

    if (!(userdata_ & IL_HAVE_STATE))
    {
        AccountStateSF filter(
            ledger_->stateMap().family().db(), app_.getLedgerMaster());
        for (auto const& h : neededStateHashes(4, &filter))
        {
            ret.push_back(
                std::make_pair(protocol::TMGetObjectByHash::otSTATE_NODE, h));
        }
    }

    if (!(userdata_ & IL_HAVE_TXNS))
    {
        TransactionStateSF filter(
            ledger_->txMap().family().db(), app_.getLedgerMaster());
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

    if (userdata_.fetch_or(IL_RECEIVE_DISPATCHED) & IL_RECEIVE_DISPATCHED)
        return false;

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
    if (packet.type() == protocol::liBASE)
    {
        if (packet.nodes().empty())
        {
            JLOG(journal_.warn()) << peer->id() << ": empty header data";
            peer->charge(Resource::feeInvalidRequest);
            return -1;
        }

        SHAMapAddNode san;

        ScopedLockType sl(mtx_);

        try
        {
            if (!(userdata_ & IL_HAVE_HEADER))
            {
                if (!takeHeader(packet.nodes(0).nodedata()))
                {
                    JLOG(journal_.warn()) << "Got invalid header data";
                    peer->charge(Resource::feeInvalidRequest);
                    return -1;
                }

                san.incUseful();
            }

            if (!(userdata_ & IL_HAVE_STATE) && (packet.nodes().size() > 1) &&
                !takeAsRootNode(makeSlice(packet.nodes(1).nodedata()), san))
            {
                JLOG(journal_.warn()) << "Included AS root invalid";
            }

            if (!(userdata_ & IL_HAVE_TXNS) && (packet.nodes().size() > 2) &&
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
            makeProgress();

        mStats += san;
        return san.getGood();
    }

    if ((packet.type() == protocol::liTX_NODE) ||
        (packet.type() == protocol::liAS_NODE))
    {
        std::string type = packet.type() == protocol::liTX_NODE ? "liTX_NODE: "
                                                                : "liAS_NODE: ";

        if (packet.nodes().empty())
        {
            JLOG(journal_.info()) << peer->id() << ": response with no nodes";
            peer->charge(Resource::feeInvalidRequest);
            return -1;
        }

        ScopedLockType sl(mtx_);

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

        JLOG(journal_.debug())
            << "Ledger "
            << ((packet.type() == protocol::liTX_NODE) ? "TX" : "AS")
            << " node stats: " << san.get();

        if (san.isUseful())
            makeProgress();

        mStats += san;
        return san.getGood();
    }

    return -1;
}

namespace detail {
// Track the amount of useful data that each peer returns
struct PeerDataCounts
{
    // Map from peer to amount of useful the peer returned
    std::unordered_map<std::shared_ptr<Peer>, int> counts;
    // The largest amount of useful data that any peer returned
    int maxCount = 0;

    // Update the data count for a peer
    void
    update(std::shared_ptr<Peer>&& peer, int dataCount)
    {
        if (dataCount <= 0)
            return;
        maxCount = std::max(maxCount, dataCount);
        auto i = counts.find(peer);
        if (i == counts.end())
        {
            counts.emplace(std::move(peer), dataCount);
            return;
        }
        i->second = std::max(i->second, dataCount);
    }

    // Prune all the peers that didn't return enough data.
    void
    prune()
    {
        // Remove all the peers that didn't return at least half as much data as
        // the best peer
        auto const thresh = maxCount / 2;
        auto i = counts.begin();
        while (i != counts.end())
        {
            if (i->second < thresh)
                i = counts.erase(i);
            else
                ++i;
        }
    }

    // call F with the `peer` parameter with a random sample of at most n values
    // of the counts vector.
    template <class F>
    void
    sampleN(std::size_t n, F&& f)
    {
        if (counts.empty())
            return;

        auto outFunc = [&f](auto&& v) { f(v.first); };
        std::minstd_rand rng{std::random_device{}()};
#if _MSC_VER
        std::vector<std::pair<std::shared_ptr<Peer>, int>> s;
        s.reserve(n);
        std::sample(
            counts.begin(), counts.end(), std::back_inserter(s), n, rng);
        for (auto& v : s)
        {
            outFunc(v);
        }
#else
        std::sample(
            counts.begin(),
            counts.end(),
            boost::make_function_output_iterator(outFunc),
            n,
            rng);
#endif
    }
};
}  // namespace detail

/** Process pending TMLedgerData
    Query the a random sample of the 'best' peers
*/
void
InboundLedger::runData()
{
    // Maximum number of peers to request data from
    constexpr std::size_t maxUsefulPeers = 6;

    decltype(mReceivedData) data;

    // Reserve some memory so the first couple iterations don't reallocate
    data.reserve(8);

    detail::PeerDataCounts dataCounts;

    for (;;)
    {
        data.clear();

        {
            std::lock_guard sl(mReceivedDataLock);

            if (mReceivedData.empty())
            {
                userdata_ &= ~IL_RECEIVE_DISPATCHED;
                break;
            }

            data.swap(mReceivedData);
        }

        for (auto& entry : data)
        {
            if (auto peer = entry.first.lock())
            {
                int count = processData(peer, *(entry.second));
                dataCounts.update(std::move(peer), count);
            }
        }
    }

    // Select a random sample of the peers that gives us the most nodes that are
    // useful
    dataCounts.prune();
    dataCounts.sampleN(maxUsefulPeers, [&](std::shared_ptr<Peer> const& peer) {
        trigger(peer, TriggerReason::reply);
    });
}

Json::Value
InboundLedger::getJson(int)
{
    Json::Value ret(Json::objectValue);

    ScopedLockType sl(mtx_);

    ret[jss::hash] = to_string(hash_);

    if (hasCompleted())
        ret[jss::complete] = true;

    if (hasFailed())
        ret[jss::failed] = true;

    if (!hasCompleted() && !hasFailed())
        ret[jss::peers] = static_cast<int>(mPeerSet->getPeerIds().size());

    auto [haveHeader, haveState, haveTransactions] = [](std::uint8_t flags) {
        return std::make_tuple(
            (flags & IL_HAVE_HEADER) == IL_HAVE_HEADER,
            (flags & IL_HAVE_STATE) == IL_HAVE_STATE,
            (flags & IL_HAVE_TXNS) == IL_HAVE_TXNS);
    }(userdata_);

    ret[jss::have_header] = haveHeader;

    if (haveHeader)
    {
        ret[jss::have_state] = haveState;

        if (haveHeader && !haveState)
        {
            Json::Value hv(Json::arrayValue);
            for (auto const& h : neededStateHashes(16, nullptr))
                hv.append(to_string(h));
            ret[jss::needed_state_hashes] = hv;
        }

        ret[jss::have_transactions] = haveTransactions;

        if (haveHeader && !haveTransactions)
        {
            Json::Value hv(Json::arrayValue);
            for (auto const& h : neededTxHashes(16, nullptr))
                hv.append(to_string(h));
            ret[jss::needed_transaction_hashes] = hv;
        }
    }

    ret[jss::timeouts] = timeouts_;

    return ret;
}

}  // namespace ripple
