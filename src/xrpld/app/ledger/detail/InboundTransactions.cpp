#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/InboundTransactions.h>
#include <xrpld/app/ledger/detail/TransactionAcquire.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/core/JobQueue.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/RippleLedgerHash.h>
#include <xrpl/resource/Fees.h>

#include <memory>
#include <mutex>

namespace ripple {

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
    std::uint32_t mSeq;
    TransactionAcquire::pointer mAcquire;
    std::shared_ptr<SHAMap> mSet;

    InboundTransactionSet(std::uint32_t seq, std::shared_ptr<SHAMap> const& set)
        : mSeq(seq), mSet(set)
    {
        ;
    }
    InboundTransactionSet() : mSeq(0)
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
        , m_seq(0)
        , m_zeroSet(m_map[uint256()])
        , m_gotSet(std::move(gotSet))
        , m_peerSetBuilder(std::move(peerSetBuilder))
        , j_(app_.journal("InboundTransactions"))
    {
        m_zeroSet.mSet = std::make_shared<SHAMap>(
            SHAMapType::TRANSACTION, uint256(), app_.getNodeFamily());
        m_zeroSet.mSet->setUnbacked();
    }

    TransactionAcquire::pointer
    getAcquire(uint256 const& hash)
    {
        {
            std::lock_guard sl(mLock);

            auto it = m_map.find(hash);

            if (it != m_map.end())
                return it->second.mAcquire;
        }
        return {};
    }

    std::shared_ptr<SHAMap>
    getSet(uint256 const& hash, bool acquire) override
    {
        TransactionAcquire::pointer ta;

        {
            std::lock_guard sl(mLock);

            if (auto it = m_map.find(hash); it != m_map.end())
            {
                if (acquire)
                {
                    it->second.mSeq = m_seq;
                    if (it->second.mAcquire)
                    {
                        it->second.mAcquire->stillNeed();
                    }
                }
                return it->second.mSet;
            }

            if (!acquire || stopping_)
                return std::shared_ptr<SHAMap>();

            ta = std::make_shared<TransactionAcquire>(
                app_, hash, m_peerSetBuilder->build());

            auto& obj = m_map[hash];
            obj.mAcquire = ta;
            obj.mSeq = m_seq;
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
    giveSet(
        uint256 const& hash,
        std::shared_ptr<SHAMap> const& set,
        bool fromAcquire) override
    {
        bool isNew = true;

        {
            std::lock_guard sl(mLock);

            auto& inboundSet = m_map[hash];

            if (inboundSet.mSeq < m_seq)
                inboundSet.mSeq = m_seq;

            if (inboundSet.mSet)
                isNew = false;
            else
                inboundSet.mSet = set;

            inboundSet.mAcquire.reset();
        }

        if (isNew)
            m_gotSet(set, fromAcquire);
    }

    void
    newRound(std::uint32_t seq) override
    {
        std::lock_guard lock(mLock);

        // Protect zero set from expiration
        m_zeroSet.mSeq = seq;

        if (m_seq != seq)
        {
            m_seq = seq;

            auto it = m_map.begin();

            std::uint32_t const minSeq =
                (seq < setKeepRounds) ? 0 : (seq - setKeepRounds);
            std::uint32_t maxSeq = seq + setKeepRounds;

            while (it != m_map.end())
            {
                if (it->second.mSeq < minSeq || it->second.mSeq > maxSeq)
                    it = m_map.erase(it);
                else
                    ++it;
            }
        }
    }

    void
    stop() override
    {
        std::lock_guard lock(mLock);
        stopping_ = true;
        m_map.clear();
    }

private:
    using MapType = hash_map<uint256, InboundTransactionSet>;

    Application& app_;

    std::recursive_mutex mLock;

    bool stopping_{false};
    MapType m_map;
    std::uint32_t m_seq;

    // The empty transaction set whose hash is zero
    InboundTransactionSet& m_zeroSet;

    std::function<void(std::shared_ptr<SHAMap> const&, bool)> m_gotSet;

    std::unique_ptr<PeerSetBuilder> m_peerSetBuilder;

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

}  // namespace ripple
