#pragma once

#include <xrpld/app/main/CollectorManager.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/shamap/Family.h>
#include <xrpl/shamap/FullBelowCache.h>
#include <xrpl/shamap/TreeNodeCache.h>

#include <cstdint>
#include <memory>
#include <mutex>

namespace xrpl {

class Application;

class NodeFamily : public Family
{
public:
    NodeFamily() = delete;
    NodeFamily(NodeFamily const&) = delete;
    NodeFamily(NodeFamily&&) = delete;

    NodeFamily&
    operator=(NodeFamily const&) = delete;

    NodeFamily&
    operator=(NodeFamily&&) = delete;

    /**
     * @param app The application whose node store and ledger master the family uses.
     * @param cm The collector for cache statistics.
     * @param clock The clock the FullBelowCache and TreeNodeCache age their entries by.
     */
    NodeFamily(Application& app, CollectorManager& cm, Stopwatch& clock = stopwatch());

    node_store::Database&
    db() override
    {
        return db_;
    }

    [[nodiscard]] node_store::Database const&
    db() const override
    {
        return db_;
    }

    beast::Journal const&
    journal() override
    {
        return j_;
    }

    std::shared_ptr<FullBelowCache>
    getFullBelowCache() override
    {
        return fbCache_;
    }

    std::shared_ptr<TreeNodeCache>
    getTreeNodeCache() override
    {
        return tnCache_;
    }

    void
    sweep() override;

    /**
     * Sweep the TreeNodeCache, and the FullBelowCache only when haveValidated is true, so a
     * first sync keeps the FullBelowCache entries it has built.
     *
     * @param haveValidated Whether the node has a validated ledger
     *        (LedgerMaster::haveValidated()).
     */
    void
    sweep(bool haveValidated);

    void
    reset() override;

    void
    missingNodeAcquireBySeq(std::uint32_t seq, UInt256 const& hash) override;

    void
    missingNodeAcquireByHash(UInt256 const& hash, std::uint32_t seq) override
    {
        acquire(hash, seq);
    }

private:
    Application& app_;
    node_store::Database& db_;
    beast::Journal const j_;

    std::shared_ptr<FullBelowCache> fbCache_;
    std::shared_ptr<TreeNodeCache> tnCache_;

    // Missing node handler
    LedgerIndex maxSeq_{0};
    std::mutex maxSeqMutex_;

    void
    acquire(UInt256 const& hash, std::uint32_t seq);
};

}  // namespace xrpl
