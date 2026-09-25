#pragma once

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/shamap/Family.h>
#include <xrpl/shamap/FullBelowCache.h>
#include <xrpl/shamap/TreeNodeCache.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace xrpl::tests {

class TestNodeFamily : public Family
{
private:
    std::unique_ptr<node_store::Database> db_;

    std::shared_ptr<FullBelowCache> fbCache_;
    std::shared_ptr<TreeNodeCache> tnCache_;

    TestStopwatch clock_;
    node_store::DummyScheduler scheduler_;

    beast::Journal const j_;

public:
    TestNodeFamily(beast::Journal j)
        : fbCache_(std::make_shared<FullBelowCache>("App family full below cache", clock_, j))
        , tnCache_(
              std::make_shared<TreeNodeCache>(
                  "App family tree node cache",
                  65536,
                  std::chrono::minutes{1},
                  clock_,
                  j))
        , j_(j)
    {
        Section testSection;
        testSection.set(Keys::kType, "memory");
        testSection.set(Keys::kPath, "SHAMap_test");
        db_ = node_store::Manager::instance().makeDatabase(
            megabytes(4), scheduler_, 1, testSection, j);
    }

    node_store::Database&
    db() override
    {
        return *db_;
    }

    [[nodiscard]] node_store::Database const&
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
    missingNodeAcquireBySeq(
        [[maybe_unused]] std::uint32_t refNum,
        [[maybe_unused]] uint256 const& nodeHash) override
    {
        Throw<std::runtime_error>("missing node");
    }

    void
    missingNodeAcquireByHash(
        [[maybe_unused]] uint256 const& refHash,
        [[maybe_unused]] std::uint32_t refNum) override
    {
        Throw<std::runtime_error>("missing node");
    }

    void
    reset() override
    {
        (*fbCache_).reset();
        (*tnCache_).reset();
    }

    TestStopwatch&
    clock()
    {
        return clock_;
    }
};

}  // namespace xrpl::tests
