#include <xrpld/app/ledger/InboundTransactions.h>

#include <xrpld/app/ledger/LedgerNodeHelpers.h>
#include <xrpld/app/ledger/detail/TransactionAcquire.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/PeerSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/protocol/RippleLedgerHash.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <xrpl.pb.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace xrpl {

// Need to be named before converting
static constexpr auto kStartPeers = 2;     // ideal number of peers to start with
static constexpr auto kSetKeepRounds = 3;  // how many rounds to keep a set

class InboundTransactionSet
{
    // A transaction set we generated, acquired, or are acquiring
public:
    std::uint32_t seq;
    TransactionAcquire::pointer acquire;
    std::shared_ptr<SHAMap> set;

    /**
     * Parent-ledger hash of the round that last requested this set. Telemetry
     * only: it tells getSet() whether a different round is asking now.
     *
     * The parent hash rather than `seq`, which is only a height: two rounds
     * STARTED on different forks at the same height are different rounds that
     * `seq` cannot separate. It does not help for a mid-round wrong-ledger
     * recovery -- see the limitation on `RCLConsensus::Adaptor`'s round members.
     */
    uint256 lastRoundParentHash;

    InboundTransactionSet(std::uint32_t seq, std::shared_ptr<SHAMap> const& set)
        : seq(seq), set(set)
    {
        ;
    }
    InboundTransactionSet() : seq(0)
    {
        ;
    }
};

class InboundTransactionsImp : public InboundTransactions
{
public:
    InboundTransactionsImp(
        Application& app,
        beast::insight::Collector::ptr const& collector,
        std::function<void(std::shared_ptr<SHAMap> const&, bool)> gotSet,
        std::unique_ptr<PeerSetBuilder> peerSetBuilder)
        : app_(app)
        , zeroSet_(map_[uint256()])
        , gotSet_(std::move(gotSet))
        , peerSetBuilder_(std::move(peerSetBuilder))
        , j_(app_.getJournal("InboundTransactions"))
    {
        zeroSet_.set =
            std::make_shared<SHAMap>(SHAMapType::TRANSACTION, uint256(), app_.getNodeFamily());
        zeroSet_.set->setUnbacked();
    }

    /**
     * Ends the span of any fetch still in flight.
     *
     * stop() normally does this, but it is only reached from
     * ApplicationImp::run(); a node torn down without run() completing destroys
     * this container directly. Without this, those fetches would reach
     * ~TransactionAcquire with an open span and trip its assertion, so the
     * enumeration of exits would be incomplete by exactly one path.
     */
    ~InboundTransactionsImp() override
    {
        std::scoped_lock const lock(lock_);
        abandonAllAcquires();
        // No map_.clear(): zeroSet_ is a reference into map_, and member
        // destruction clears it anyway.
    }

    TransactionAcquire::pointer
    getAcquire(uint256 const& hash)
    {
        {
            std::scoped_lock const sl(lock_);

            auto it = map_.find(hash);

            if (it != map_.end())
                return it->second.acquire;
        }
        return {};
    }

    std::shared_ptr<SHAMap>
    getSet(
        uint256 const& hash,
        bool acquire,
        uint256 const& roundParentHash,
        std::uint32_t roundLedgerSeq) override
    {
        TransactionAcquire::pointer ta;

        {
            std::scoped_lock const sl(lock_);

            if (auto it = map_.find(hash); it != map_.end())
            {
                if (acquire)
                {
                    // Read before the store below overwrites it. We are called
                    // once per peer proposal, so without this a set proposed by
                    // 35 validators would gain 35 events per round.
                    bool const newRequester = it->second.lastRoundParentHash != roundParentHash;

                    it->second.lastRoundParentHash = roundParentHash;

                    it->second.seq = seq_;
                    if (it->second.acquire)
                    {
                        it->second.acquire->stillNeed();

                        // A fetch is keyed by set hash and refreshed by every
                        // round that still lacks the set, so it is wanted by
                        // many rounds. Its span records each of them.
                        if (newRequester)
                        {
                            it->second.acquire->recordRequestingRound(
                                roundParentHash, roundLedgerSeq);
                        }
                    }
                }
                return it->second.set;
            }

            if (!acquire || stopping_)
                return std::shared_ptr<SHAMap>();

            // The round identity is copied into the acquire here, under the
            // lock, because init() below runs after we release it.
            ta = std::make_shared<TransactionAcquire>(
                app_, hash, peerSetBuilder_->build(), roundParentHash, roundLedgerSeq);

            auto& obj = map_[hash];
            obj.acquire = ta;
            obj.seq = seq_;
            // Recorded as the last requester because init() below fires this
            // round's event. Without it, the round that started the fetch would
            // be counted a second time by its own next proposal.
            obj.lastRoundParentHash = roundParentHash;
        }

        ta->init(kStartPeers);

        return {};
    }

    /**
     * We received a TMLedgerData from a peer.
     */
    void
    gotData(
        LedgerHash const& hash,
        std::shared_ptr<Peer> peer,
        std::shared_ptr<protocol::TMLedgerData> packetPtr) override
    {
        protocol::TMLedgerData const& packet = *packetPtr;

        JLOG(j_.trace()) << "Got data (" << packet.nodes().size()
                         << ") for acquiring ledger: " << hash;

        TransactionAcquire::pointer const ta = getAcquire(hash);

        if (ta == nullptr)
        {
            peer->charge(resource::kFeeUselessData, "ledger_data useless");
            return;
        }

        std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> data;
        data.reserve(packet.nodes().size());

        for (auto const& ledgerNode : packet.nodes())
        {
            auto treeNode = getTreeNode(ledgerNode.nodedata());
            if (!treeNode)
            {
                JLOG(j_.warn()) << "Got invalid node data for TX set " << hash << " from peer "
                                << peer->id();
                peer->charge(resource::kFeeInvalidData, "ledger_node.node_data invalid");
                return;
            }

            auto const nodeID = getSHAMapNodeID(ledgerNode, *treeNode);
            if (!nodeID)
            {
                JLOG(j_.warn()) << "Got invalid node id for TX set " << hash << " from peer "
                                << peer->id();
                peer->charge(resource::kFeeInvalidData, "ledger_node.node_id invalid");
                return;
            }

            data.emplace_back(*nodeID, std::move(treeNode));
        }

        auto const san = ta->takeNodes(std::move(data), peer);
        if (san.isInvalid())
        {
            peer->charge(resource::kFeeInvalidData, "ledger_data invalid");
        }
        else if (!san.isUseful())
        {
            peer->charge(resource::kFeeUselessData, "ledger_data useless");
        }
    }

    void
    giveSet(uint256 const& hash, std::shared_ptr<SHAMap> const& set, bool fromAcquire) override
    {
        bool isNew = true;

        {
            std::scoped_lock const sl(lock_);

            auto& inboundSet = map_[hash];

            inboundSet.seq = std::max(inboundSet.seq, seq_);

            if (inboundSet.set)
            {
                isNew = false;
            }
            else
            {
                inboundSet.set = set;
            }

            // End the acquire span here, at the point we stop pursuing the
            // fetch, rather than leaving it to whenever the last reference to
            // the object is released -- which may be a JtTxnData worker or a
            // peer callback, arbitrarily later, and would report that wait as
            // part of the fetch. Usually a no-op on this path, because the
            // common caller is the job done() queues after a successful fetch
            // that already stamped its own outcome.
            if (inboundSet.acquire)
                inboundSet.acquire->abandonAcquireSpan();

            inboundSet.acquire.reset();
        }

        if (isNew)
            gotSet_(set, fromAcquire);
    }

    void
    newRound(std::uint32_t seq) override
    {
        std::scoped_lock const lock(lock_);

        // Protect zero set from expiration
        zeroSet_.seq = seq;

        if (seq_ != seq)
        {
            seq_ = seq;

            auto it = map_.begin();

            std::uint32_t const minSeq = (seq < kSetKeepRounds) ? 0 : (seq - kSetKeepRounds);
            std::uint32_t const maxSeq = seq + kSetKeepRounds;

            while (it != map_.end())
            {
                if (it->second.seq < minSeq || it->second.seq > maxSeq)
                {
                    // The sweep is where a set that never arrived stops being
                    // pursued, so it is where its span must end. Left to the
                    // erase below, the span would instead end when the last
                    // reference dropped.
                    if (it->second.acquire)
                        it->second.acquire->abandonAcquireSpan();

                    it = map_.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
    }

    void
    stop() override
    {
        std::scoped_lock const lock(lock_);
        stopping_ = true;
        abandonAllAcquires();
        map_.clear();
    }

private:
    /**
     * Ends the span of every fetch still in flight.
     *
     * Shutdown is an exit like any other, so the spans end here rather than
     * letting the following map_.clear() decide their end time. Not virtual, so
     * it is safe to call from the destructor.
     *
     * @note Caller must hold lock_. abandonAcquireSpan() is noexcept and
     *       bounded, so this can neither throw out of a destructor nor stall
     *       teardown.
     */
    void
    abandonAllAcquires()
    {
        for (auto& entry : map_)
        {
            if (entry.second.acquire)
                entry.second.acquire->abandonAcquireSpan();
        }
    }

    using MapType = hash_map<uint256, InboundTransactionSet>;

    Application& app_;

    std::recursive_mutex lock_;

    bool stopping_{false};
    MapType map_;
    std::uint32_t seq_{0};

    // The empty transaction set whose hash is zero
    InboundTransactionSet& zeroSet_;

    std::function<void(std::shared_ptr<SHAMap> const&, bool)> gotSet_;

    std::unique_ptr<PeerSetBuilder> peerSetBuilder_;

    beast::Journal j_;
};

//------------------------------------------------------------------------------

InboundTransactions::~InboundTransactions() = default;

std::unique_ptr<InboundTransactions>
makeInboundTransactions(
    Application& app,
    beast::insight::Collector::ptr const& collector,
    std::function<void(std::shared_ptr<SHAMap> const&, bool)> gotSet)
{
    return std::make_unique<InboundTransactionsImp>(
        app, collector, std::move(gotSet), makePeerSetBuilder(app));
}

}  // namespace xrpl
