#include <xrpld/app/ledger/InboundTransactions.h>

#include <xrpld/app/ledger/detail/TransactionAcquire.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/PeerSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/protocol/RippleLedgerHash.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapNodeID.h>

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
    getSet(uint256 const& hash, bool acquire) override
    {
        TransactionAcquire::pointer ta;

        {
            std::scoped_lock const sl(lock_);

            if (auto it = map_.find(hash); it != map_.end())
            {
                if (acquire)
                {
                    it->second.seq = seq_;
                    if (it->second.acquire)
                    {
                        it->second.acquire->stillNeed();
                    }
                }
                return it->second.set;
            }

            if (!acquire || stopping_)
                return std::shared_ptr<SHAMap>();

            ta = std::make_shared<TransactionAcquire>(app_, hash, peerSetBuilder_->build());

            auto& obj = map_[hash];
            obj.acquire = ta;
            obj.seq = seq_;
        }

        ta->init(kStartPeers);

        return {};
    }

    /** We received a TMLedgerData from a peer.
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
            peer->charge(Resource::kFeeUselessData, "ledger_data");
            return;
        }

        std::vector<std::pair<SHAMapNodeID, Slice>> data;
        data.reserve(packet.nodes().size());

        for (auto const& node : packet.nodes())
        {
            if (!node.has_nodeid() || !node.has_nodedata())
            {
                peer->charge(Resource::kFeeMalformedRequest, "ledger_data");
                return;
            }

            auto const id = deserializeSHAMapNodeID(node.nodeid());

            if (!id)
            {
                peer->charge(Resource::kFeeInvalidData, "ledger_data");
                return;
            }

            data.emplace_back(*id, makeSlice(node.nodedata()));
        }

        if (!ta->takeNodes(data, peer).isUseful())
            peer->charge(Resource::kFeeUselessData, "ledger_data not useful");
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
        map_.clear();
    }

private:
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
