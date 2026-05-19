#include <xrpld/app/ledger/InboundLedger.h>

#include <xrpld/app/ledger/AccountStateSF.h>
#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/TransactionStateSF.h>
#include <xrpld/app/ledger/detail/TimeoutCounter.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/Overlay.h>
#include <xrpld/overlay/PeerSet.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/json/json_value.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapSyncFilter.h>

#include <boost/iterator/function_output_iterator.hpp>

#include <xrpl.pb.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xrpl {

using namespace std::chrono_literals;

static constexpr auto kPeerCountStart = 5;           // Number of peers to start with
static constexpr auto kPeerCountAdd = 3;             // Number of peers to add on a timeout
static constexpr auto kLedgerTimeoutRetriesMax = 6;  // how many timeouts before we give up
static constexpr auto kLedgerBecomeAggressiveThreshold =
    4;                                          // how many timeouts before we get aggressive
static constexpr auto kMissingNodesFind = 256;  // Number of nodes to find initially
static constexpr auto kReqNodesReply = 128;     // Number of nodes to request for a reply
static constexpr auto kReqNodes = 12;           // Number of nodes to request blindly

// millisecond for each ledger timeout
constexpr auto kLedgerAcquireTimeout = 3000ms;

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
          kLedgerAcquireTimeout,
          {.jobType = JtLedgerData, .jobName = "InboundLedger", .jobLimit = 5},
          app.getJournal("InboundLedger"))
    , clock_(clock)
    , seq_(seq)
    , reason_(reason)
    , peerSet_(std::move(peerSet))
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
        addPeers();
        queueJob(sl);
        return;
    }

    JLOG(journal_.debug()) << "Acquiring ledger we already have in "
                           << " local store. " << hash_;
    XRPL_ASSERT(
        ledger_->header().seq < kXrpLedgerEarliestFees || ledger_->read(keylet::fees()),
        "xrpl::InboundLedger::init : valid ledger fees");
    ledger_->setImmutable();

    if (reason_ == Reason::HISTORY)
        return;

    app_.getLedgerMaster().storeLedger(ledger_);

    // Check if this could be a newer fully-validated ledger
    if (reason_ == Reason::CONSENSUS)
        app_.getLedgerMaster().checkAccept(ledger_);
}

std::size_t
InboundLedger::getPeerCount() const
{
    auto const& peerIds = peerSet_->getPeerIds();
    return std::count_if(peerIds.begin(), peerIds.end(), [this](auto id) {
        return (app_.getOverlay().findPeerByShortID(id) != nullptr);
    });
}

void
InboundLedger::update(std::uint32_t seq)
{
    ScopedLockType const sl(mtx_);

    // If we didn't know the sequence number, but now do, save it
    if ((seq != 0) && (seq_ == 0))
        seq_ = seq;

    // Prevent this from being swept
    touch();
}

bool
InboundLedger::checkLocal()
{
    ScopedLockType const sl(mtx_);
    if (!isDone())
    {
        if (ledger_)
        {
            tryDB(ledger_->stateMap().family().db());
        }
        else
        {
            tryDB(app_.getNodeFamily().db());
        }
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
    for (auto& entry : receivedData_)
    {
        if (entry.second->type() == protocol::liAS_NODE)
            app_.getInboundLedgers().gotStaleData(entry.second);
    }
    if (!isDone())
    {
        JLOG(journal_.debug()) << "Acquire " << hash_ << " abort "
                               << ((timeouts_ == 0) ? std::string()
                                                    : (std::string("timeouts:") +
                                                       std::to_string(timeouts_) + " "))
                               << stats_.get();
    }
}

static std::vector<uint256>
neededHashes(uint256 const& root, SHAMap& map, int max, SHAMapSyncFilter* filter)
{
    std::vector<uint256> ret;

    if (!root.isZero())
    {
        if (map.getHash().isZero())
        {
            ret.push_back(root);
        }
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
    return neededHashes(ledger_->header().txHash, ledger_->txMap(), max, filter);
}

std::vector<uint256>
InboundLedger::neededStateHashes(int max, SHAMapSyncFilter* filter) const
{
    return neededHashes(ledger_->header().accountHash, ledger_->stateMap(), max, filter);
}

// See how much of the ledger data is stored locally
// Data found in a fetch pack will be stored
void
InboundLedger::tryDB(NodeStore::Database& srcDB)
{
    if (!haveHeader_)
    {
        auto makeLedger = [&, this](Blob const& data) {
            JLOG(journal_.trace()) << "Ledger header found in fetch pack";
            Rules const rules{app_.config().features};
            ledger_ = std::make_shared<Ledger>(
                deserializePrefixedHeader(makeSlice(data)), rules, app_.getNodeFamily());
            if (ledger_->header().hash != hash_ || (seq_ != 0 && seq_ != ledger_->header().seq))
            {
                // We know for a fact the ledger can never be acquired
                JLOG(journal_.warn())
                    << "hash " << hash_ << " seq " << std::to_string(seq_) << " cannot be a ledger";
                ledger_.reset();
                failed_ = true;
            }
        };

        // Try to fetch the ledger header from the DB
        if (auto nodeObject = srcDB.fetchNodeObject(hash_, seq_))
        {
            JLOG(journal_.trace()) << "Ledger header found in local store";

            makeLedger(nodeObject->getData());
            if (failed_)
                return;

            // Store the ledger header if the source and destination differ
            auto& dstDB{ledger_->stateMap().family().db()};
            if (std::addressof(dstDB) != std::addressof(srcDB))
            {
                Blob blob{nodeObject->getData()};
                dstDB.store(NodeObjectType::Ledger, std::move(blob), hash_, ledger_->header().seq);
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
            ledger_->stateMap().family().db().store(
                NodeObjectType::Ledger, std::move(*data), hash_, ledger_->header().seq);
        }

        if (seq_ == 0)
            seq_ = ledger_->header().seq;
        ledger_->stateMap().setLedgerSeq(seq_);
        ledger_->txMap().setLedgerSeq(seq_);
        haveHeader_ = true;
    }

    if (!haveTransactions_)
    {
        if (ledger_->header().txHash.isZero())
        {
            JLOG(journal_.trace()) << "No TXNs to fetch";
            haveTransactions_ = true;
        }
        else
        {
            TransactionStateSF filter(ledger_->txMap().family().db(), app_.getLedgerMaster());
            if (ledger_->txMap().fetchRoot(SHAMapHash{ledger_->header().txHash}, &filter))
            {
                if (neededTxHashes(1, &filter).empty())
                {
                    JLOG(journal_.trace()) << "Had full txn map locally";
                    haveTransactions_ = true;
                }
            }
        }
    }

    if (!haveState_)
    {
        if (ledger_->header().accountHash.isZero())
        {
            JLOG(journal_.fatal()) << "We are acquiring a ledger with a zero account hash";
            failed_ = true;
            return;
        }
        AccountStateSF filter(ledger_->stateMap().family().db(), app_.getLedgerMaster());
        if (ledger_->stateMap().fetchRoot(SHAMapHash{ledger_->header().accountHash}, &filter))
        {
            if (neededStateHashes(1, &filter).empty())
            {
                JLOG(journal_.trace()) << "Had full AS map locally";
                haveState_ = true;
            }
        }
    }

    if (haveTransactions_ && haveState_)
    {
        JLOG(journal_.debug()) << "Had everything locally";
        complete_ = true;
        XRPL_ASSERT(
            ledger_->header().seq < kXrpLedgerEarliestFees || ledger_->read(keylet::fees()),
            "xrpl::InboundLedger::tryDB : valid ledger fees");
        ledger_->setImmutable();
    }
}

/** Called with a lock by the PeerSet when the timer expires
 */
void
InboundLedger::onTimer(bool wasProgress, ScopedLockType&)
{
    recentNodes_.clear();

    if (isDone())
    {
        JLOG(journal_.info()) << "Already done " << hash_;
        return;
    }

    if (timeouts_ > kLedgerTimeoutRetriesMax)
    {
        if (seq_ != 0)
        {
            JLOG(journal_.warn()) << timeouts_ << " timeouts for ledger " << seq_;
        }
        else
        {
            JLOG(journal_.warn()) << timeouts_ << " timeouts for ledger " << hash_;
        }
        failed_ = true;
        done();
        return;
    }

    if (!wasProgress)
    {
        checkLocal();

        byHash_ = true;

        std::size_t const pc = getPeerCount();
        JLOG(journal_.debug()) << "No progress(" << pc << ") for ledger " << hash_;

        // addPeers triggers if the reason is not HISTORY
        // So if the reason IS HISTORY, need to trigger after we add
        // otherwise, we need to trigger before we add
        // so each peer gets triggered once
        if (reason_ != Reason::HISTORY)
            trigger(nullptr, TriggerReason::Timeout);
        addPeers();
        if (reason_ == Reason::HISTORY)
            trigger(nullptr, TriggerReason::Timeout);
    }
}

/** Add more peers to the set, if possible */
void
InboundLedger::addPeers()
{
    peerSet_->addPeers(
        (getPeerCount() == 0) ? kPeerCountStart : kPeerCountAdd,
        [this](auto peer) { return peer->hasLedger(hash_, seq_); },
        [this](auto peer) {
            // For historical nodes, do not trigger too soon
            // since a fetch pack is probably coming
            if (reason_ != Reason::HISTORY)
                trigger(peer, TriggerReason::Added);
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
    if (signaled_)
        return;

    signaled_ = true;
    touch();

    JLOG(journal_.debug()) << "Acquire " << hash_ << (failed_ ? " fail " : " ")
                           << ((timeouts_ == 0)
                                   ? std::string()
                                   : (std::string("timeouts:") + std::to_string(timeouts_) + " "))
                           << stats_.get();

    XRPL_ASSERT(complete_ || failed_, "xrpl::InboundLedger::done : complete or failed");

    if (complete_ && !failed_ && ledger_)
    {
        XRPL_ASSERT(
            ledger_->header().seq < kXrpLedgerEarliestFees || ledger_->read(keylet::fees()),
            "xrpl::InboundLedger::done : valid ledger fees");
        ledger_->setImmutable();
        switch (reason_)
        {
            case Reason::HISTORY:
                app_.getInboundLedgers().onLedgerFetched();
                break;
            default:
                app_.getLedgerMaster().storeLedger(ledger_);
                break;
        }
    }

    // We hold the PeerSet lock, so must dispatch
    app_.getJobQueue().addJob(JtLedgerData, "AcqDone", [self = shared_from_this()]() {
        if (self->complete_ && !self->failed_)
        {
            self->app_.getLedgerMaster().checkAccept(self->getLedger());
            self->app_.getLedgerMaster().tryAdvance();
        }
        else
        {
            self->app_.getInboundLedgers().logFailure(self->hash_, self->seq_);
        }
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
        JLOG(journal_.debug()) << "Trigger on ledger: " << hash_ << (complete_ ? " completed" : "")
                               << (failed_ ? " failed" : "");
        return;
    }

    if (auto stream = journal_.debug())
    {
        std::stringstream ss;
        ss << "Trigger acquiring ledger " << hash_;
        if (peer)
            ss << " from " << peer;

        if (complete_ || failed_)
        {
            ss << " complete=" << complete_ << " failed=" << failed_;
        }
        else
        {
            ss << " header=" << haveHeader_ << " tx=" << haveTransactions_ << " as=" << haveState_;
        }
        stream << ss.str();
    }

    if (!haveHeader_)
    {
        tryDB(app_.getNodeFamily().db());
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

        if (!progress_ && !failed_ && byHash_ && (timeouts_ > kLedgerBecomeAggressiveThreshold))
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
                        if (seq_ != 0)
                            io->set_ledgerseq(seq_);
                    }
                }

                auto packet = std::make_shared<Message>(tmBH, protocol::mtGET_OBJECTS);
                auto const& peerIds = peerSet_->getPeerIds();
                std::ranges::for_each(peerIds, [this, &packet](auto id) {
                    if (auto p = app_.getOverlay().findPeerByShortID(id))
                    {
                        byHash_ = false;
                        p->send(packet);
                    }
                });
            }
            else
            {
                JLOG(journal_.info()) << "getNeededHashes says acquire is complete";
                haveHeader_ = true;
                haveTransactions_ = true;
                haveState_ = true;
                complete_ = true;
            }
        }
    }

    // We can't do much without the header data because we don't know the
    // state or transaction root hashes.
    if (!haveHeader_ && !failed_)
    {
        tmGL.set_itype(protocol::liBASE);
        if (seq_ != 0)
            tmGL.set_ledgerseq(seq_);
        JLOG(journal_.trace()) << "Sending header request to "
                               << (peer ? "selected peer" : "all peers");
        peerSet_->sendRequest(tmGL, peer);
        return;
    }

    if (ledger_)
        tmGL.set_ledgerseq(ledger_->header().seq);

    if (reason != TriggerReason::Reply)
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
    {
        tmGL.set_querydepth(1);
    }

    // Get the state data first because it's the most likely to be useful
    // if we wind up abandoning this fetch.
    if (haveHeader_ && !haveState_ && !failed_)
    {
        XRPL_ASSERT(
            ledger_,
            "xrpl::InboundLedger::trigger : non-null ledger to read state "
            "from");

        if (!ledger_->stateMap().isValid())
        {
            failed_ = true;
        }
        else if (ledger_->stateMap().getHash().isZero())
        {
            // we need the root node
            tmGL.set_itype(protocol::liAS_NODE);
            *tmGL.add_nodeids() = SHAMapNodeID().getRawString();
            JLOG(journal_.trace())
                << "Sending AS root request to " << (peer ? "selected peer" : "all peers");
            peerSet_->sendRequest(tmGL, peer);
            return;
        }
        else
        {
            AccountStateSF filter(ledger_->stateMap().family().db(), app_.getLedgerMaster());

            // Release the lock while we process the large state map
            sl.unlock();
            auto nodes = ledger_->stateMap().getMissingNodes(kMissingNodesFind, &filter);
            sl.lock();

            // Make sure nothing happened while we released the lock
            if (!failed_ && !complete_ && !haveState_)
            {
                if (nodes.empty())
                {
                    if (!ledger_->stateMap().isValid())
                    {
                        failed_ = true;
                    }
                    else
                    {
                        haveState_ = true;

                        if (haveTransactions_)
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

                        JLOG(journal_.trace()) << "Sending AS node request (" << nodes.size()
                                               << ") to " << (peer ? "selected peer" : "all peers");
                        peerSet_->sendRequest(tmGL, peer);
                        return;
                    }

                    JLOG(journal_.trace()) << "All AS nodes filtered";
                }
            }
        }
    }

    if (haveHeader_ && !haveTransactions_ && !failed_)
    {
        XRPL_ASSERT(
            ledger_,
            "xrpl::InboundLedger::trigger : non-null ledger to read "
            "transactions from");

        if (!ledger_->txMap().isValid())
        {
            failed_ = true;
        }
        else if (ledger_->txMap().getHash().isZero())
        {
            // we need the root node
            tmGL.set_itype(protocol::liTX_NODE);
            *(tmGL.add_nodeids()) = SHAMapNodeID().getRawString();
            JLOG(journal_.trace())
                << "Sending TX root request to " << (peer ? "selected peer" : "all peers");
            peerSet_->sendRequest(tmGL, peer);
            return;
        }
        else
        {
            TransactionStateSF filter(ledger_->txMap().family().db(), app_.getLedgerMaster());

            auto nodes = ledger_->txMap().getMissingNodes(kMissingNodesFind, &filter);

            if (nodes.empty())
            {
                if (!ledger_->txMap().isValid())
                {
                    failed_ = true;
                }
                else
                {
                    haveTransactions_ = true;

                    if (haveState_)
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
                    JLOG(journal_.trace()) << "Sending TX node request (" << nodes.size() << ") to "
                                           << (peer ? "selected peer" : "all peers");
                    peerSet_->sendRequest(tmGL, peer);
                    return;
                }

                JLOG(journal_.trace()) << "All TX nodes filtered";
            }
        }
    }

    if (complete_ || failed_)
    {
        JLOG(journal_.debug()) << "Done:" << (complete_ ? " complete" : "")
                               << (failed_ ? " failed " : " ") << ledger_->header().seq;
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
    auto dup = std::ranges::stable_partition(
        nodes, [this](auto const& item) { return recentNodes_.count(item.second) == 0; });

    // If everything is a duplicate we don't want to send
    // any query at all except on a timeout where we need
    // to query everyone:
    if (dup.begin() == nodes.begin())
    {
        JLOG(journal_.trace()) << "filterNodes: all duplicates";

        if (reason != TriggerReason::Timeout)
        {
            nodes.clear();
            return;
        }
    }
    else
    {
        JLOG(journal_.trace()) << "filterNodes: pruning duplicates";

        nodes.erase(dup.begin(), dup.end());
    }

    std::size_t const limit = (reason == TriggerReason::Reply) ? kReqNodesReply : kReqNodes;

    if (nodes.size() > limit)
        nodes.resize(limit);

    for (auto const& n : nodes)
        recentNodes_.insert(n.second);
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

    if (complete_ || failed_ || haveHeader_)
        return true;

    auto* f = &app_.getNodeFamily();
    Rules const rules{app_.config().features};
    ledger_ = std::make_shared<Ledger>(deserializeHeader(makeSlice(data)), rules, *f);
    if (ledger_->header().hash != hash_ || (seq_ != 0 && seq_ != ledger_->header().seq))
    {
        JLOG(journal_.warn()) << "Acquire hash mismatch: " << ledger_->header().hash
                              << "!=" << hash_;
        ledger_.reset();
        return false;
    }
    if (seq_ == 0)
        seq_ = ledger_->header().seq;
    ledger_->stateMap().setLedgerSeq(seq_);
    ledger_->txMap().setLedgerSeq(seq_);
    haveHeader_ = true;

    Serializer s(data.size() + 4);
    s.add32(HashPrefix::LedgerMaster);
    s.addRaw(data.data(), data.size());
    f->db().store(NodeObjectType::Ledger, std::move(s.modData()), hash_, seq_);

    if (ledger_->header().txHash.isZero())
        haveTransactions_ = true;

    if (ledger_->header().accountHash.isZero())
        haveState_ = true;

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
    if (!haveHeader_)
    {
        JLOG(journal_.warn()) << "Missing ledger header";
        san.incInvalid();
        return;
    }
    if (packet.type() == protocol::liTX_NODE)
    {
        if (haveTransactions_ || failed_)
        {
            san.incDuplicate();
            return;
        }
    }
    else if (haveState_ || failed_)
    {
        san.incDuplicate();
        return;
    }

    auto [map, rootHash, filter] =
        [&]() -> std::tuple<SHAMap&, SHAMapHash, std::unique_ptr<SHAMapSyncFilter>> {
        if (packet.type() == protocol::liTX_NODE)
        {
            return {
                ledger_->txMap(),
                SHAMapHash{ledger_->header().txHash},
                std::make_unique<TransactionStateSF>(
                    ledger_->txMap().family().db(), app_.getLedgerMaster())};
        }
        return {
            ledger_->stateMap(),
            SHAMapHash{ledger_->header().accountHash},
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
        {
            haveTransactions_ = true;
        }
        else
        {
            haveState_ = true;
        }

        if (haveTransactions_ && haveState_)
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
    if (failed_ || haveState_)
    {
        san.incDuplicate();
        return true;
    }

    if (!haveHeader_)
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::InboundLedger::takeAsRootNode : no ledger header");
        return false;
        // LCOV_EXCL_STOP
    }

    AccountStateSF filter(ledger_->stateMap().family().db(), app_.getLedgerMaster());
    san +=
        ledger_->stateMap().addRootNode(SHAMapHash{ledger_->header().accountHash}, data, &filter);
    return san.isGood();
}

/** Process AS root node received from a peer
    Call with a lock
*/
bool
InboundLedger::takeTxRootNode(Slice const& data, SHAMapAddNode& san)
{
    if (failed_ || haveTransactions_)
    {
        san.incDuplicate();
        return true;
    }

    if (!haveHeader_)
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::InboundLedger::takeTxRootNode : no ledger header");
        return false;
        // LCOV_EXCL_STOP
    }

    TransactionStateSF filter(ledger_->txMap().family().db(), app_.getLedgerMaster());
    san += ledger_->txMap().addRootNode(SHAMapHash{ledger_->header().txHash}, data, &filter);
    return san.isGood();
}

std::vector<InboundLedger::neededHash_t>
InboundLedger::getNeededHashes()
{
    std::vector<neededHash_t> ret;

    if (!haveHeader_)
    {
        ret.emplace_back(protocol::TMGetObjectByHash::otLEDGER, hash_);
        return ret;
    }

    if (!haveState_)
    {
        AccountStateSF filter(ledger_->stateMap().family().db(), app_.getLedgerMaster());
        for (auto const& h : neededStateHashes(4, &filter))
        {
            ret.emplace_back(protocol::TMGetObjectByHash::otSTATE_NODE, h);
        }
    }

    if (!haveTransactions_)
    {
        TransactionStateSF filter(ledger_->txMap().family().db(), app_.getLedgerMaster());
        for (auto const& h : neededTxHashes(4, &filter))
        {
            ret.emplace_back(protocol::TMGetObjectByHash::otTRANSACTION_NODE, h);
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
    std::scoped_lock const sl(receivedDataLock_);

    if (isDone())
        return false;

    receivedData_.emplace_back(peer, data);

    if (receiveDispatched_)
        return false;

    receiveDispatched_ = true;
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
InboundLedger::processData(std::shared_ptr<Peer> peer, protocol::TMLedgerData& packet)
{
    if (packet.type() == protocol::liBASE)
    {
        if (packet.nodes().empty())
        {
            JLOG(journal_.warn()) << peer->id() << ": empty header data";
            peer->charge(Resource::kFeeMalformedRequest, "ledger_data empty header");
            return -1;
        }

        SHAMapAddNode san;

        ScopedLockType const sl(mtx_);

        try
        {
            if (!haveHeader_)
            {
                if (!takeHeader(packet.nodes(0).nodedata()))
                {
                    JLOG(journal_.warn()) << "Got invalid header data";
                    peer->charge(Resource::kFeeMalformedRequest, "ledger_data invalid header");
                    return -1;
                }

                san.incUseful();
            }

            if (!haveState_ && (packet.nodes().size() > 1) &&
                !takeAsRootNode(makeSlice(packet.nodes(1).nodedata()), san))
            {
                JLOG(journal_.warn()) << "Included AS root invalid";
            }

            if (!haveTransactions_ && (packet.nodes().size() > 2) &&
                !takeTxRootNode(makeSlice(packet.nodes(2).nodedata()), san))
            {
                JLOG(journal_.warn()) << "Included TX root invalid";
            }
        }
        catch (std::exception const& ex)
        {
            JLOG(journal_.warn()) << "Included AS/TX root invalid: " << ex.what();
            using namespace std::string_literals;
            peer->charge(Resource::kFeeInvalidData, "ledger_data "s + ex.what());
            return -1;
        }

        if (san.isUseful())
            progress_ = true;

        stats_ += san;
        return san.getGood();
    }

    if ((packet.type() == protocol::liTX_NODE) || (packet.type() == protocol::liAS_NODE))
    {
        if (packet.nodes().empty())
        {
            JLOG(journal_.info()) << peer->id() << ": response with no nodes";
            peer->charge(Resource::kFeeMalformedRequest, "ledger_data no nodes");
            return -1;
        }

        ScopedLockType const sl(mtx_);

        // Verify node IDs and data are complete
        for (auto const& node : packet.nodes())
        {
            if (!node.has_nodeid() || !node.has_nodedata())
            {
                JLOG(journal_.warn()) << "Got bad node";
                peer->charge(Resource::kFeeMalformedRequest, "ledger_data bad node");
                return -1;
            }
        }

        SHAMapAddNode san;
        receiveNode(packet, san);

        JLOG(journal_.debug()) << "Ledger "
                               << ((packet.type() == protocol::liTX_NODE) ? "TX" : "AS")
                               << " node stats: " << san.get();

        if (san.isUseful())
            progress_ = true;

        stats_ += san;
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
            {
                i = counts.erase(i);
            }
            else
            {
                ++i;
            }
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
        std::sample(counts.begin(), counts.end(), std::back_inserter(s), n, rng);
        for (auto& v : s)
        {
            outFunc(v);
        }
#else
        std::sample(
            counts.begin(), counts.end(), boost::make_function_output_iterator(outFunc), n, rng);
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
    static constexpr std::size_t kMaxUsefulPeers = 6;

    decltype(receivedData_) data;

    // Reserve some memory so the first couple iterations don't reallocate
    data.reserve(8);

    detail::PeerDataCounts dataCounts;

    for (;;)
    {
        data.clear();

        {
            std::scoped_lock const sl(receivedDataLock_);

            if (receivedData_.empty())
            {
                receiveDispatched_ = false;
                break;
            }

            data.swap(receivedData_);
        }

        for (auto& entry : data)
        {
            if (auto peer = entry.first.lock())
            {
                int const count = processData(peer, *(entry.second));
                dataCounts.update(std::move(peer), count);
            }
        }
    }

    // Select a random sample of the peers that gives us the most nodes that are
    // useful
    dataCounts.prune();
    dataCounts.sampleN(kMaxUsefulPeers, [&](std::shared_ptr<Peer> const& peer) {
        trigger(peer, TriggerReason::Reply);
    });
}

json::Value
InboundLedger::getJson(int)
{
    json::Value ret(json::ValueType::Object);

    ScopedLockType const sl(mtx_);

    ret[jss::hash] = to_string(hash_);

    if (complete_)
        ret[jss::complete] = true;

    if (failed_)
        ret[jss::failed] = true;

    if (!complete_ && !failed_)
        ret[jss::peers] = static_cast<int>(peerSet_->getPeerIds().size());

    ret[jss::have_header] = haveHeader_;

    if (haveHeader_)
    {
        ret[jss::have_state] = haveState_;
        ret[jss::have_transactions] = haveTransactions_;
    }

    ret[jss::timeouts] = timeouts_;

    if (haveHeader_ && !haveState_)
    {
        json::Value hv(json::ValueType::Array);
        for (auto const& h : neededStateHashes(16, nullptr))
        {
            hv.append(to_string(h));
        }
        ret[jss::needed_state_hashes] = hv;
    }

    if (haveHeader_ && !haveTransactions_)
    {
        json::Value hv(json::ValueType::Array);
        for (auto const& h : neededTxHashes(16, nullptr))
        {
            hv.append(to_string(h));
        }
        ret[jss::needed_transaction_hashes] = hv;
    }

    return ret;
}

}  // namespace xrpl
