#include <xrpl/nodestore/Backend.h>

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/temp_dir.h>
#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Types.h>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>
#include <nodestore/TestBase.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <ranges>
#include <string>
#include <thread>
#include <vector>

namespace xrpl::node_store {

namespace {

std::vector<std::string>
backendTypes()
{
    std::vector<std::string> types{"nudb"};
#if XRPL_ROCKSDB_AVAILABLE
    types.emplace_back("rocksdb");
#endif
#ifdef XRPL_ENABLE_SQLITE_BACKEND_TESTS
    types.emplace_back("sqlite");
#endif
    return types;
}

// Run work(i) for every i in [0, n) spread across numThreads threads, handing
// out indices via a shared atomic counter (mirrors the old Timing_test
// parallel-for so the N items are partitioned, not duplicated).
template <class Work>
void
parallelFor(std::size_t n, std::size_t numThreads, Work work)
{
    std::atomic<std::size_t> next{0};
    auto const runner = [&] {
        for (std::size_t i = next++; i < n; i = next++)
            work(i);
    };

    auto threads = std::views::iota(std::size_t{0}, numThreads) |
        std::views::transform([&](std::size_t) { return std::thread{runner}; }) |
        std::ranges::to<std::vector>();

    std::ranges::for_each(threads, &std::thread::join);
}

}  // namespace

class BackendTypeTest : public ::testing::TestWithParam<std::string>
{
protected:
    void
    SetUp() override
    {
        params_.set("type", GetParam());
        params_.set("path", tempDir_.path());

        beast::xor_shift_engine rng(kSeedValue);
        batch_ = createPredictableBatch(kNumObjects, rng());
    }

    std::unique_ptr<Backend>
    makeOpenBackend()
    {
        auto backend = Manager::instance().makeBackend(params_, megabytes(4), scheduler_, journal_);
        backend->open();
        return backend;
    }

    DummyScheduler scheduler_;
    beast::TempDir const tempDir_;
    beast::Journal const journal_{TestSink::instance()};
    Section params_;
    Batch batch_;
};

TEST_P(BackendTypeTest, store_and_fetch)
{
    auto backend = makeOpenBackend();
    storeBatch(*backend, batch_);

    {
        SCOPED_TRACE("read in original order");
        auto const copy = fetchCopyOfBatch(*backend, batch_);
        EXPECT_EQ(batch_, copy);
    }

    {
        SCOPED_TRACE("read in shuffled order");
        beast::xor_shift_engine rng(kSeedValue);
        std::shuffle(batch_.begin(), batch_.end(), rng);
        auto const copy = fetchCopyOfBatch(*backend, batch_);
        EXPECT_EQ(batch_, copy);
    }
}

TEST_P(BackendTypeTest, persists_after_reopen)
{
    {
        auto backend = makeOpenBackend();
        storeBatch(*backend, batch_);
    }

    // re-open a fresh backend instance over the same path
    auto backend = makeOpenBackend();
    auto copy = fetchCopyOfBatch(*backend, batch_);
    std::ranges::sort(batch_, LessThan{});
    std::ranges::sort(copy, LessThan{});
    EXPECT_EQ(batch_, copy);
}

// missing-key path. Replaces the correctness half of Timing_test::doMissing
// (and the missing branch of doMixed): every fetch on an empty backend must
// report Status::NotFound.
TEST_P(BackendTypeTest, fetch_missing)
{
    auto backend = makeOpenBackend();
    // deliberately do NOT store batch_ — every key must be absent
    fetchMissing(*backend, batch_);
}

// concurrent store/fetch correctness. Replaces the correctness half of the
// multi-threaded Timing_test workloads (which only ran manually, never in CI):
// many threads store disjoint objects, then many threads fetch and verify each
// round-trips. Doubles as a thread-safety smoke test for the backend.
TEST_P(BackendTypeTest, concurrent_store_and_fetch)
{
    // The SQLite backend is not designed for concurrent writers (and the old
    // Timing_test only exercised nudb/rocksdb under threads).
    if (GetParam() == "sqlite")
        GTEST_SKIP() << "sqlite backend is not exercised under concurrency";

    for (auto const numThreads : {4uz, 8uz})
    {
        SCOPED_TRACE("threads=" + std::to_string(numThreads));

        auto backend = makeOpenBackend();

        // concurrent stores of disjoint objects
        parallelFor(batch_.size(), numThreads, [&](std::size_t i) { backend->store(batch_[i]); });

        // concurrent fetches, each verifying its object round-trips. Worker
        // threads only touch an atomic counter; the EXPECT runs on the main
        // thread after join to avoid relying on cross-thread assertion support.
        std::atomic<std::size_t> mismatches{0};
        parallelFor(batch_.size(), numThreads, [&](std::size_t i) {
            std::shared_ptr<NodeObject> result;
            if (backend->fetch(batch_[i]->getHash(), &result) != Status::Ok || !result ||
                !isSame(result, batch_[i]))
            {
                ++mismatches;
            }
        });
        EXPECT_EQ(mismatches.load(), 0u);

        backend->close();
    }
}

INSTANTIATE_TEST_SUITE_P(
    BackendTypes,
    BackendTypeTest,
    ::testing::ValuesIn(backendTypes()),
    [](::testing::TestParamInfo<std::string> const& info) { return info.param; });

}  // namespace xrpl::node_store
