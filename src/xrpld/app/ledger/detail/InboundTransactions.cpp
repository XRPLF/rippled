#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/InboundTransactions.h>
#include <xrpld/app/ledger/detail/TransactionAcquire.h>
#include <xrpld/app/main/Application.h>

#include <xrpl/basics/Log.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/protocol/RippleLedgerHash.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/server/NetworkOPs.h>

#include <memory>
#include <mutex>

namespace xrpl {

enum {
    // Ideal number of peers to start with
    startPeers = 2,

    // How many rounds to keep a set
    setKeepRounds = 3,
};

class InboundTransactionSet
{
    // A transaction set we generated, acquired, or are acquiring
public:
    std::uint32_t seq_;
    TransactionAcquire::pointer acquire_;
    std::shared_ptr<SHAMap> set_;

    InboundTransactionSet(std::uint32_t seq, std::shared_ptr<SHAMap> const& set)
        : seq_(seq), set_(set)
    {
        ;
    }
    InboundTransactionSet() : seq_(0)
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
        , seq_(0)
        , zeroSet_(map_[uint256()])
        , gotSet_(std::move(gotSet))
        , peerSetBuilder_(std::move(peerSetBuilder))
        , j_(app_.journal("InboundTransactions"))
    {
        zeroSet_.set_ =
            std::make_shared<SHAMap>(SHAMapType::TRANSACTION, uint256(), app_.getNodeFamily());
        zeroSet_.set_->setUnbacked();
    }

    TransactionAcquire::pointer
    getAcquire(uint256 const& hash)
    {
        {
            std::lock_guard sl(lock_);

            auto it = map_.find(hash);

            if (it != map_.end())
                return it->second.acquire_;
        }
        return {};
    }

    std::shared_ptr<SHAMap>
    getSet(uint256 const& hash, bool acquire) override
    {
        TransactionAcquire::pointer ta;

        {
            std::lock_guard sl(lock_);

            if (auto it = map_.find(hash); it != map_.end())
            {
                if (acquire)
                {
                    it->second.seq_ = seq_;
                    if (it->second.acquire_)
                    {
                        it->second.acquire_->stillNeed();
                    }
                }
                return it->second.set_;
            }

            if (!acquire || stopping_)
                return std::shared_ptr<SHAMap>();

            ta = std::make_shared<TransactionAcquire>(app_, hash, peerSetBuilder_->build());

            auto& obj = map_[hash];
            obj.acquire_ = ta;
            obj.seq_ = seq_;
        }

        ta->init(startPeers);

        return {};
    }

    /** We received a TMLedgerData from a peer.
     */
    void
    gotData(
        LedgerHash const& hash,
        std::shared_ptr<Peer> peer,
        std::shared_ptr<protocol::TMLedgerData> packet_ptr) override
    {
        protocol::TMLedgerData& packet = *packet_ptr;

        JLOG(j_.trace()) << "Got data (" << packet.nodes().size()
                         << ") for acquiring ledger: " << hash;

        TransactionAcquire::pointer ta = getAcquire(hash);

        if (ta == nullptr)
        {
            peer->charge(Resource::feeUselessData, "ledger_data");
            return;
        }

        std::vector<std::pair<SHAMapNodeID, Slice>> data;
        data.reserve(packet.nodes().size());

        for (auto const& node : packet.nodes())
        {
            if (!node.has_nodeid() || !node.has_nodedata())
            {
                peer->charge(Resource::feeMalformedRequest, "ledger_data");
                return;
            }

            auto const id = deserializeSHAMapNodeID(node.nodeid());

            if (!id)
            {
                peer->charge(Resource::feeInvalidData, "ledger_data");
                return;
            }

            data.emplace_back(std::make_pair(*id, makeSlice(node.nodedata())));
        }

        if (!ta->takeNodes(data, peer).isUseful())
            peer->charge(Resource::feeUselessData, "ledger_data not useful");
    }

    void
    giveSet(uint256 const& hash, std::shared_ptr<SHAMap> const& set, bool fromAcquire) override
    {
        bool isNew = true;

        {
            std::lock_guard sl(lock_);

            auto& inboundSet = map_[hash];

            if (inboundSet.seq_ < seq_)
                inboundSet.seq_ = seq_;

            if (inboundSet.set_)
                isNew = false;
            else
                inboundSet.set_ = set;

            inboundSet.acquire_.reset();
        }

        if (isNew)
            gotSet_(set, fromAcquire);
    }

    void
    newRound(std::uint32_t seq) override
    {
        std::lock_guard lock(lock_);

        // Protect zero set from expiration
        zeroSet_.seq_ = seq;

        if (seq_ != seq)
        {
            seq_ = seq;

            auto it = map_.begin();

            std::uint32_t const minSeq = (seq < setKeepRounds) ? 0 : (seq - setKeepRounds);
            std::uint32_t maxSeq = seq + setKeepRounds;

            while (it != map_.end())
            {
                if (it->second.seq_ < minSeq || it->second.seq_ > maxSeq)
                    it = map_.erase(it);
                else
                    ++it;
            }
        }
    }

    void
    stop() override
    {
        std::lock_guard lock(lock_);
        stopping_ = true;
        map_.clear();
    }

private:
    using MapType = hash_map<uint256, InboundTransactionSet>;

    Application& app_;

    std::recursive_mutex lock_;

    bool stopping_{false};
    MapType map_;
    std::uint32_t seq_;

    // The empty transaction set whose hash is zero
    InboundTransactionSet& zeroSet_;

    std::function<void(std::shared_ptr<SHAMap> const&, bool)> gotSet_;

    std::unique_ptr<PeerSetBuilder> peerSetBuilder_;

    beast::Journal j_;
};

//------------------------------------------------------------------------------

InboundTransactions::~InboundTransactions() = default;

std::unique_ptr<InboundTransactions>
make_InboundTransactions(
    Application& app,
    beast::insight::Collector::ptr const& collector,
    std::function<void(std::shared_ptr<SHAMap> const&, bool)> gotSet)
{
    return std::make_unique<InboundTransactionsImp>(
        app, collector, std::move(gotSet), make_PeerSetBuilder(app));
}

}  // namespace xrpl
