#ifndef XRPL_SHAMAP_NODEFAMILY_H_INCLUDED
#define XRPL_SHAMAP_NODEFAMILY_H_INCLUDED

#include <xrpl/shamap/Family.h>

namespace ripple {

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

    NodeFamily(Application& app, CollectorManager& cm);

    NodeStore::Database&
    db() override
    {
        return db_;
    }

    NodeStore::Database const&
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

    void
    reset() override;

    void
    missingNodeAcquireBySeq(std::uint32_t seq, uint256 const& hash) override;

    void
    missingNodeAcquireByHash(uint256 const& hash, std::uint32_t seq) override
    {
        acquire(hash, seq);
    }

private:
    Application& app_;
    NodeStore::Database& db_;
    beast::Journal const j_;

    std::shared_ptr<FullBelowCache> fbCache_;
    std::shared_ptr<TreeNodeCache> tnCache_;

    // Missing node handler
    LedgerIndex maxSeq_{0};
    std::mutex maxSeqMutex_;

    void
    acquire(uint256 const& hash, std::uint32_t seq);
};

}  // namespace ripple

#endif
