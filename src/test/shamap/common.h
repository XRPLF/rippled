#ifndef XRPL_SHAMAP_TESTS_COMMON_H_INCLUDED
#define XRPL_SHAMAP_TESTS_COMMON_H_INCLUDED

#include <xrpl/basics/chrono.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/shamap/Family.h>

namespace ripple {
namespace tests {

class TestNodeFamily : public Family
{
private:
    std::unique_ptr<NodeStore::Database> db_;

    std::shared_ptr<FullBelowCache> fbCache_;
    std::shared_ptr<TreeNodeCache> tnCache_;

    TestStopwatch clock_;
    NodeStore::DummyScheduler scheduler_;

    beast::Journal const j_;

public:
    TestNodeFamily(beast::Journal j)
        : fbCache_(std::make_shared<FullBelowCache>(
              "App family full below cache",
              clock_,
              j))
        , tnCache_(std::make_shared<TreeNodeCache>(
              "App family tree node cache",
              65536,
              std::chrono::minutes{1},
              clock_,
              j))
        , j_(j)
    {
        Section testSection;
        testSection.set("type", "memory");
        testSection.set("path", "SHAMap_test");
        db_ = NodeStore::Manager::instance().make_Database(
            megabytes(4), scheduler_, 1, testSection, j);
    }

    NodeStore::Database&
    db() override
    {
        return *db_;
    }

    NodeStore::Database const&
    db() const override
    {
        return *db_;
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
    sweep() override
    {
        fbCache_->sweep();
        tnCache_->sweep();
    }

    void
    missingNodeAcquireBySeq(std::uint32_t refNum, uint256 const& nodeHash)
        override
    {
        Throw<std::runtime_error>("missing node");
    }

    void
    missingNodeAcquireByHash(uint256 const& refHash, std::uint32_t refNum)
        override
    {
        Throw<std::runtime_error>("missing node");
    }

    void
    reset() override
    {
        fbCache_->reset();
        tnCache_->reset();
    }

    beast::manual_clock<std::chrono::steady_clock>
    clock()
    {
        return clock_;
    }
};

}  // namespace tests
}  // namespace ripple

#endif
