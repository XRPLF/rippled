#pragma once

#include <xrpl/basics/chrono.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/shamap/Family.h>

#include <memory>

namespace xrpl::test {

/** Test implementation of Family for unit tests.

    Uses an in-memory NodeStore database and simple caches.
    The missingNode methods throw since tests shouldn't encounter missing nodes.
*/
class TestFamily : public Family
{
private:
    std::unique_ptr<NodeStore::Database> db_;
    TestStopwatch clock_;
    std::shared_ptr<FullBelowCache> fbCache_;
    std::shared_ptr<TreeNodeCache> tnCache_;
    NodeStore::DummyScheduler scheduler_;
    beast::Journal j_;

public:
    explicit TestFamily(beast::Journal j)
        : fbCache_(std::make_shared<FullBelowCache>("TestFamily full below cache", clock_, j))
        , tnCache_(
              std::make_shared<TreeNodeCache>(
                  "TestFamily tree node cache",
                  65536,
                  std::chrono::minutes{1},
                  clock_,
                  j))
        , j_(j)
    {
        Section config;
        config.set(Keys::kType, "memory");
        config.set(Keys::kPath, "TestFamily");
        db_ = NodeStore::Manager::instance().makeDatabase(megabytes(4), scheduler_, 1, config, j);
    }

    NodeStore::Database&
    db() override
    {
        return *db_;
    }

    [[nodiscard]] NodeStore::Database const&
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
    missingNodeAcquireBySeq(std::uint32_t refNum, uint256 const& nodeHash) override
    {
        Throw<std::runtime_error>("TestFamily: missing node (by seq)");
    }

    void
    missingNodeAcquireByHash(uint256 const& refHash, std::uint32_t refNum) override
    {
        Throw<std::runtime_error>("TestFamily: missing node (by hash)");
    }

    void
    reset() override
    {
        (*fbCache_).reset();
        (*tnCache_).reset();
    }

    /** Access the test clock for time manipulation in tests. */
    TestStopwatch&
    clock()
    {
        return clock_;
    }
};

}  // namespace xrpl::test
