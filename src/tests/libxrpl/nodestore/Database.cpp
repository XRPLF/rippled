#include <xrpl/nodestore/Database.h>

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/temp_dir.h>
#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Task.h>
#include <xrpl/nodestore/Types.h>
#include <xrpl/nodestore/WriteStats.h>
#include <xrpl/protocol/SystemParameters.h>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>
#include <nodestore/TestBase.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace xrpl::node_store {

namespace {

std::vector<std::string>
allBackends()
{
    std::vector<std::string> types{"memory", "nudb"};
#if XRPL_ROCKSDB_AVAILABLE
    types.emplace_back("rocksdb");
#endif
    return types;
}

std::vector<std::string>
persistentBackends()
{
    std::vector<std::string> types{"nudb"};
#if XRPL_ROCKSDB_AVAILABLE
    types.emplace_back("rocksdb");
#endif
    return types;
}

std::vector<std::string>
importBackends()
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

/**
 * A scheduler that records what the nodestore reports about each operation.
 *
 * The production scheduler forwards these reports to the job queue and to
 * telemetry, so capturing them here lets a test observe exactly the values a
 * dashboard would receive.
 *
 *   Database::fetchNodeObject()
 *        |
 *        +-- FetchReport{elapsed, wasFound} --> onFetch()
 *
 *   Database::store() -> backend
 *        |
 *        +-- BatchWriteReport{elapsed, writeCount} --> onBatchWrite()
 *
 * @note Counters are atomic because Scheduler is called from the nodestore's
 *       read threads in production. The tests below fetch synchronously on
 *       one thread, so the totals are still exact.
 */
struct CapturingScheduler : Scheduler
{
    std::atomic<std::uint64_t> totalReportedUs{0};  ///< Sum of reported fetch durations.
    std::atomic<std::uint64_t> fetchCount{0};       ///< Fetch reports received.
    std::atomic<std::uint64_t> foundCount{0};       ///< Fetch reports with wasFound set.
    std::atomic<std::uint64_t> batchWriteCount{0};  ///< Batch-write reports received.

    /**
     * Fetch reports whose duration is not a whole number of milliseconds.
     *
     * A millisecond-typed duration converts to a whole multiple of 1000
     * microseconds, so it can never be counted here however fast or slow the
     * machine is. A non-zero count therefore proves the report carried
     * sub-millisecond resolution, and it proves it without assuming anything
     * about how long a read takes.
     */
    std::atomic<std::uint64_t> subMillisecondCount{0};

    void
    scheduleTask(Task& task) override
    {
        task.performScheduledTask();
    }

    void
    onFetch(FetchReport const& report) override
    {
        // duration_cast, not .count(), so this compiles against either unit
        // and the test can be run before and after the fix.
        auto const us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(report.elapsed).count());

        totalReportedUs += us;
        ++fetchCount;
        if (report.wasFound)
            ++foundCount;
        if (us % 1000 != 0)
            ++subMillisecondCount;
    }

    void
    onBatchWrite(BatchWriteReport const&) override
    {
        ++batchWriteCount;
    }
};

}  // namespace

// Shared setup for the parameterized Database tests: builds the node params,
// journal and a predictable batch per test, mirroring Backend.cpp's fixture.
class NodeStoreDatabaseTestBase : public ::testing::TestWithParam<std::string>
{
protected:
    void
    SetUp() override
    {
        nodeParams_.set("type", GetParam());
        nodeParams_.set("path", nodeDb_.path());

        beast::xor_shift_engine rng(kSeedValue);
        batch_ = createPredictableBatch(kNumObjects, rng());
    }

    std::unique_ptr<Database>
    makeDatabase()
    {
        return Manager::instance().makeDatabase(megabytes(4), scheduler_, 2, nodeParams_, journal_);
    }

    DummyScheduler scheduler_;
    beast::TempDir const nodeDb_;
    beast::Journal const journal_{TestSink::instance()};
    Section nodeParams_;
    Batch batch_;
};

class NodeStoreDatabaseTest : public NodeStoreDatabaseTestBase
{
};

class NodeStoreDatabasePersistenceTest : public NodeStoreDatabaseTestBase
{
};

TEST_P(NodeStoreDatabaseTest, store_and_fetch)
{
    auto db = makeDatabase();

    storeBatch(*db, batch_);

    {
        SCOPED_TRACE("read in original order");
        auto const copy = fetchCopyOfBatch(*db, batch_);
        EXPECT_EQ(batch_, copy);
    }

    {
        SCOPED_TRACE("read in shuffled order");
        beast::xor_shift_engine rng(kSeedValue);
        std::shuffle(batch_.begin(), batch_.end(), rng);
        auto const copy = fetchCopyOfBatch(*db, batch_);
        EXPECT_EQ(batch_, copy);
    }
}

TEST_P(NodeStoreDatabasePersistenceTest, round_trip)
{
    {
        auto db = makeDatabase();
        storeBatch(*db, batch_);
    }

    // re-open without the ephemeral db
    auto db = makeDatabase();

    auto copy = fetchCopyOfBatch(*db, batch_);
    std::ranges::sort(batch_, LessThan{});
    std::ranges::sort(copy, LessThan{});
    EXPECT_EQ(batch_, copy);
}

// missing-key path at the Database layer. Mirrors Backend's fetch_missing —
// fetching keys that were never stored must return nullptr (NotFound).
TEST_P(NodeStoreDatabaseTest, fetch_missing)
{
    auto db = makeDatabase();

    // never store: every key must be absent
    fetchMissing(*db, batch_);
}

// Database::getWriteStats() forwards to the backend, and the telemetry
// exporter branches on the optional to decide whether to publish the NuDB
// write-queue labels at all. Backend.cpp covers the backend's own answer;
// this covers the forwarding, which is what the exporter actually calls.
TEST_P(NodeStoreDatabaseTest, write_stats_forwarded_from_backend)
{
    auto db = makeDatabase();

    // Before any write, only a measuring backend answers at all.
    auto const initial = db->getWriteStats();
    ASSERT_EQ(initial.has_value(), GetParam() == "nudb")
        << "only nudb measures its write path; backend=" << GetParam();

    if (!initial)
    {
        // Negative path: a non-measuring backend must keep reporting absence
        // after real writes too, so the exporter omits the labels rather
        // than publishing zeros that would read as an idle write path.
        storeBatch(*db, batch_);
        EXPECT_FALSE(db->getWriteStats().has_value());
        return;
    }

    // A freshly opened store has written nothing.
    EXPECT_EQ(initial->insertCount, 0u);
    EXPECT_EQ(initial->insertTotalUs, 0u);
    EXPECT_EQ(initial->depthSum, 0u);
    EXPECT_EQ(initial->concurrentWriters, 0u);

    storeBatch(*db, batch_);

    // Every object stored through the Database reaches the backend exactly
    // once, so the forwarded count is the batch size and not, say, the
    // number of store() batches.
    auto const after = db->getWriteStats();
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->insertCount, batch_.size());
    // One writer thread, so the depth recorded at each insert is exactly 1.
    EXPECT_EQ(after->depthSum, batch_.size());
    // No writer is left in flight once the calls have returned.
    EXPECT_EQ(after->concurrentWriters, 0u);
    EXPECT_GT(after->insertTotalUs, 0u);
    // A maximum is never below the mean, which fails if the field held the
    // minimum or the first sample instead of a running maximum.
    EXPECT_GE(after->insertMaxUs * after->insertCount, after->insertTotalUs);
}

// Read latency is the discriminator between a warm nodestore and a cold one:
// a warm store answers in single-digit microseconds and a cold one in low
// hundreds, while the hit rate reads the same for both. FetchReport::elapsed
// is what the production scheduler forwards, so if that field cannot hold a
// sub-millisecond value the difference is gone before anything can record it.
//
// nudb rather than memory: a real store's reads take microseconds of
// measurable work, whereas an in-memory map lookup can finish inside a single
// microsecond tick and report a legitimate zero.
TEST(NodeStoreDatabase, sub_millisecond_fetch_latency_is_reported)
{
    CapturingScheduler scheduler;
    beast::TempDir const nodeDb;
    Section nodeParams;
    nodeParams.set("type", "nudb");
    nodeParams.set("path", nodeDb.path());

    beast::Journal const journal(TestSink::instance());

    // No cache_size/cache_age, so DatabaseNodeImp builds no cache and every
    // fetch reaches the backend. That keeps the counts exact.
    auto db = Manager::instance().makeDatabase(megabytes(4), scheduler, 2, nodeParams, journal);
    ASSERT_NE(db, nullptr);

    // Nothing has been fetched, so nothing has been reported. This also
    // proves the counters below are moved by this test and not by setup.
    ASSERT_EQ(scheduler.fetchCount.load(), 0u);
    ASSERT_EQ(scheduler.totalReportedUs.load(), 0u);
    ASSERT_EQ(db->getFetchDurationUs(), 0u);

    constexpr std::size_t kNumStored = 256;
    auto const stored = createPredictableBatch(kNumStored, kSeedValue);
    ASSERT_EQ(stored.size(), kNumStored);
    storeBatch(*db, stored);

    // Each store reaches the backend once, and nudb reports every insert as a
    // one-object batch write. A change to BatchWriteReport that broke its
    // millisecond contract would show up here rather than silently.
    EXPECT_EQ(scheduler.batchWriteCount.load(), kNumStored);

    // Positive path: every object is present, so every fetch is a hit.
    for (auto const& object : stored)
        EXPECT_NE(db->fetchNodeObject(object->getHash(), 0), nullptr);

    // Every fetch produced exactly one report, and each one saw its object.
    ASSERT_EQ(scheduler.fetchCount.load(), kNumStored);
    EXPECT_EQ(scheduler.foundCount.load(), kNumStored);

    // The nodestore's own microsecond accumulator moved, so there is real
    // measured time for the reports to carry.
    auto const internalUs = db->getFetchDurationUs();
    ASSERT_GT(internalUs, 0u);

    // The core assertion. Database::fetchNodeObject() measures each fetch once
    // and uses that one value for both the internal accumulator and the
    // report, so the two totals must agree exactly. A millisecond-typed report
    // truncates every sub-millisecond fetch to zero and this fails.
    EXPECT_EQ(scheduler.totalReportedUs.load(), internalUs);

    // Independent of the equality above, and independent of how fast this
    // machine is: a millisecond-typed duration always converts to a whole
    // multiple of 1000 microseconds, so at least one report carrying a
    // non-multiple proves the field itself holds sub-millisecond resolution.
    EXPECT_GT(scheduler.subMillisecondCount.load(), 0u);

    // Negative path: a miss is still a fetch, so it is still reported and
    // still timed, but it is not a hit. A report that only fired on hits
    // would hide exactly the cold-read case this measures.
    constexpr std::size_t kNumMissing = 32;
    auto const missing = createPredictableBatch(kNumMissing, kSeedValue + 1);
    ASSERT_EQ(missing.size(), kNumMissing);
    for (auto const& object : missing)
        EXPECT_EQ(db->fetchNodeObject(object->getHash(), 0), nullptr);

    EXPECT_EQ(scheduler.fetchCount.load(), kNumStored + kNumMissing);
    EXPECT_EQ(scheduler.foundCount.load(), kNumStored);

    // The totals still agree once misses are included, so the miss path
    // reports exactly what it measured rather than substituting a zero.
    //
    // GE and not GT: a cumulative total cannot shrink, but an individual miss
    // served from NuDB's in-memory buckets can genuinely measure under one
    // microsecond and truncate to zero, so requiring growth here would be a
    // statement about this machine's speed rather than about the code.
    auto const internalUsWithMisses = db->getFetchDurationUs();
    EXPECT_GE(internalUsWithMisses, internalUs);
    EXPECT_EQ(scheduler.totalReportedUs.load(), internalUsWithMisses);

    // Reads perform no writes, so the write-report count cannot have moved.
    EXPECT_EQ(scheduler.batchWriteCount.load(), kNumStored);
}

INSTANTIATE_TEST_SUITE_P(
    NodeStoreBackends,
    NodeStoreDatabaseTest,
    ::testing::ValuesIn(allBackends()),
    [](::testing::TestParamInfo<std::string> const& info) { return info.param; });

INSTANTIATE_TEST_SUITE_P(
    PersistentBackends,
    NodeStoreDatabasePersistenceTest,
    ::testing::ValuesIn(persistentBackends()),
    [](::testing::TestParamInfo<std::string> const& info) { return info.param; });

TEST(NodeStoreDatabase, memory_earliest_seq)
{
    DummyScheduler scheduler;
    beast::TempDir const nodeDb;
    Section nodeParams;
    nodeParams.set("type", "memory");
    nodeParams.set("path", nodeDb.path());

    beast::Journal const journal(TestSink::instance());

    // default earliest ledger sequence
    {
        auto db = Manager::instance().makeDatabase(megabytes(4), scheduler, 2, nodeParams, journal);
        EXPECT_EQ(db->earliestLedgerSeq(), kXrpLedgerEarliestSeq);
    }

    // invalid earliest_seq value
    {
        nodeParams.set("earliest_seq", "0");
        try
        {
            auto db =
                Manager::instance().makeDatabase(megabytes(4), scheduler, 2, nodeParams, journal);
            FAIL() << "expected runtime_error for earliest_seq=0";
        }
        catch (std::runtime_error const& e)
        {
            EXPECT_STREQ(e.what(), "Invalid earliest_seq");
        }
    }

    // valid earliest_seq value
    {
        nodeParams.set("earliest_seq", "1");
        auto db = Manager::instance().makeDatabase(megabytes(4), scheduler, 2, nodeParams, journal);
        EXPECT_EQ(db->earliestLedgerSeq(), 1u);
    }
}

class DatabaseImportTest : public ::testing::TestWithParam<std::string>
{
};

TEST_P(DatabaseImportTest, same_backend)
{
    auto const type = GetParam();

    DummyScheduler scheduler;
    beast::Journal const journal(TestSink::instance());

    beast::TempDir const srcDir;
    Section srcParams;
    srcParams.set("type", type);
    srcParams.set("path", srcDir.path());

    auto batch = createPredictableBatch(kNumObjects, kSeedValue);

    // write to source db
    {
        auto src = Manager::instance().makeDatabase(megabytes(4), scheduler, 2, srcParams, journal);
        storeBatch(*src, batch);
    }

    Batch copy;
    {
        // re-open source and import into a fresh destination
        auto src = Manager::instance().makeDatabase(megabytes(4), scheduler, 2, srcParams, journal);

        beast::TempDir const destDir;
        Section destParams;
        destParams.set("type", type);
        destParams.set("path", destDir.path());

        auto dest =
            Manager::instance().makeDatabase(megabytes(4), scheduler, 2, destParams, journal);

        dest->importDatabase(*src);
        copy = fetchCopyOfBatch(*dest, batch);
    }

    std::ranges::sort(batch, LessThan{});
    std::ranges::sort(copy, LessThan{});
    EXPECT_EQ(batch, copy);
}

INSTANTIATE_TEST_SUITE_P(
    ImportBackends,
    DatabaseImportTest,
    ::testing::ValuesIn(importBackends()),
    [](::testing::TestParamInfo<std::string> const& info) { return info.param; });

}  // namespace xrpl::node_store
