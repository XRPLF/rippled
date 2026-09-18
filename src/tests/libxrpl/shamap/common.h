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

#include <atomic>
#include <chrono>
#include <cstddef>
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

    // Written from whichever nodestore reader thread reports the miss, so read back atomically.
    std::atomic<std::size_t> missingBySeqReports_ = 0;
    std::atomic<std::uint32_t> missingBySeqRefNum_ = 0;

public:
    /**
     * @param j The journal to log through.
     * @param readThreads How many nodestore reader threads to run asynchronous
     *        fetches on. More than one is needed only by a test that wants
     *        several reads to complete on different threads at once.
     */
    explicit TestNodeFamily(beast::Journal j, int readThreads = 1)
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
            megabytes(4), scheduler_, readThreads, testSection, j);
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

    /**
     * Record the report and throw, standing in for Family's real acquisition
     * machinery.
     *
     * @param refNum Sequence of the ledger with the missing node, recorded so a
     *        test can check which sequence the map published.
     * @param nodeHash Hash of the missing node. Unused.
     */
    void
    missingNodeAcquireBySeq(std::uint32_t refNum, [[maybe_unused]] uint256 const& nodeHash) override
    {
        missingBySeqRefNum_.store(refNum, std::memory_order_release);
        ++missingBySeqReports_;
        Throw<std::runtime_error>("missing node");
    }

    /**
     * Throw, standing in for Family's real acquisition machinery. Uncounted, as
     * no test in this suite drives this path.
     *
     * @param refHash Hash of the ledger with the missing node. Unused.
     * @param refNum Sequence of the ledger with the missing node. Unused.
     */
    void
    missingNodeAcquireByHash(
        [[maybe_unused]] uint256 const& refHash,
        [[maybe_unused]] std::uint32_t refNum) override
    {
        Throw<std::runtime_error>("missing node");
    }

    /**
     * How many times a map of this family has withdrawn its claim of being
     * complete in the database. Counted per family, so a test that wants the
     * count for one map has to give that map a family of its own.
     *
     * @return The number of missingNodeAcquireBySeq() calls so far.
     */
    [[nodiscard]] std::size_t
    missingBySeqReports() const
    {
        return missingBySeqReports_.load(std::memory_order_acquire);
    }

    /**
     * The ledger sequence the most recent such report named, which is the hint
     * the map published for the nodestore lookup that has to resolve the gap.
     *
     * @return The sequence, or zero if nothing has been reported yet.
     */
    [[nodiscard]] std::uint32_t
    missingBySeqRefNum() const
    {
        return missingBySeqRefNum_.load(std::memory_order_acquire);
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
