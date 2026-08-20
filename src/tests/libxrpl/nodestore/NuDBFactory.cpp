#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/FileUtilities.h>
#include <xrpl/basics/scope.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/Types.h>
#include <xrpl/nodestore/WriteStats.h>

#include <gtest/gtest.h>
#include <helpers/CaptureSink.h>
#include <helpers/TestSink.h>
#include <nodestore/TestBase.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <latch>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace xrpl::node_store {

namespace {

Section
makeSection(std::string const& path, std::string const& blockSize = "")
{
    Section params;
    params.set("type", "nudb");
    params.set("path", path);
    if (!blockSize.empty())
        params.set("nudb_block_size", blockSize);
    return params;
}

void
runRoundTrip(Section const& params, std::size_t expectedBlocksize)
{
    DummyScheduler scheduler;
    beast::Journal const journal(TestSink::instance());
    auto backend = Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);

    ASSERT_TRUE(backend);
    ASSERT_EQ(backend->getBlockSize(), expectedBlocksize);
    backend->open();
    ASSERT_TRUE(backend->isOpen());

    auto const batch = createPredictableBatch(10, 12345);
    storeBatch(*backend, batch);

    auto const copy = fetchCopyOfBatch(*backend, batch);

    backend->close();
    EXPECT_EQ(batch, copy);
}

/**
 * Threads used by the overlapping-insert round below.
 */
constexpr std::uint64_t kOverlapThreads = 8;

/**
 * Inserts each of those threads performs per round.
 */
constexpr std::uint64_t kOverlapPerThread = 50;

/**
 * Run one round of deliberately overlapping inserts against @p backend.
 *
 * All kOverlapThreads threads are released from a single latch, so they reach
 * doInsert() together rather than one after another; staggered starts are what
 * would let every insert run end to end and never overlap.
 *
 * The latch is the whole point of the round, and it is also the one thing here
 * that can hang the test binary rather than fail it. Two rules keep it safe:
 *
 *   1. Every batch is built before the first thread exists, so the only thing
 *      a thread does before arriving is arrive. Building a batch inside the
 *      thread allocates, so it can throw, and a thread that throws never
 *      arrives -- leaving the other seven blocked on the latch for good.
 *   2. Spawning is guarded, so a thread that never starts still has its
 *      arrival accounted for.
 *
 *     batches built here (may throw; no thread waiting yet)
 *          |
 *          v
 *     spawn 8 --> [ latch: 8 arrivals ] --> stores overlap --> join
 *          |                  ^
 *          +-- spawn threw ---+  guard counts down the missing arrivals,
 *                                then joins the threads already running
 *
 * @param backend Backend to insert into. Must be open.
 * @param round   Round index, mixed into the seeds so every round writes
 *                fresh keys and no insert takes the duplicate short-circuit.
 */
void
runOverlappingInsertRound(Backend& backend, int round)
{
    std::vector<Batch> batches;
    batches.reserve(kOverlapThreads);
    for (auto t = 0uz; t < kOverlapThreads; ++t)
    {
        batches.push_back(createPredictableBatch(
            kOverlapPerThread, 1000 + t + (static_cast<std::uint64_t>(round) * 100'000)));
    }

    std::latch start(static_cast<std::ptrdiff_t>(kOverlapThreads));

    std::vector<std::thread> threads;
    threads.reserve(kOverlapThreads);

    // Covers a spawn loop that ends early. The latch is built for the full set
    // because a thread that has already arrived cannot be un-counted, so the
    // shortfall is counted down instead of the latch being resized. Counting
    // down comes before joining: a thread left waiting on the latch would never
    // become joinable.
    //
    // Released once every thread exists, so the success path joins below rather
    // than from a destructor -- join() can throw, and a destructor would turn
    // that into a terminate.
    ScopeExit releaseAndJoin([&] {
        start.count_down(static_cast<std::ptrdiff_t>(kOverlapThreads - threads.size()));
        for (auto& th : threads)
            th.join();
    });

    for (auto t = 0uz; t < kOverlapThreads; ++t)
    {
        threads.emplace_back([&backend, &start, &batches, t] {
            start.arrive_and_wait();
            for (auto const& obj : batches[t])
                backend.store(obj);
        });
    }
    releaseAndJoin.release();

    for (auto& th : threads)
        th.join();
}

}  // namespace

TEST(NuDBFactory, default_block_size)
{
    TempDir const tempDir;
    auto const params = makeSection(tempDir.path());
    ASSERT_NO_FATAL_FAILURE(runRoundTrip(params, 4096));
}

TEST(NuDBFactory, valid_block_sizes)
{
    auto const kValidSizes = std::to_array<std::size_t>({4096, 8192, 16384, 32768});
    for (auto const size : kValidSizes)
    {
        SCOPED_TRACE("size=" + std::to_string(size));
        TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), std::to_string(size));
        ASSERT_NO_FATAL_FAILURE(runRoundTrip(params, size));
    }

    // empty value is ignored by config parser; default (4096) is used
    {
        TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), "");
        ASSERT_NO_FATAL_FAILURE(runRoundTrip(params, 4096));
    }
}

TEST(NuDBFactory, invalid_block_sizes)
{
    std::vector<std::string> const kInvalidSizes = {
        "2048",     // too small
        "1024",     // too small
        "65536",    // too large
        "131072",   // too large
        "5000",     // not power of 2
        "6000",     // not power of 2
        "10000",    // not power of 2
        "0",        // zero
        "-1",       // negative
        "abc",      // non-numeric
        "4k",       // invalid format
        "4096.5"};  // decimal

    for (auto const& size : kInvalidSizes)
    {
        SCOPED_TRACE("size='" + size + "'");
        TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), size);
        EXPECT_THROW(runRoundTrip(params, 4096), std::exception);
    }

    // whitespace handling — lexical_cast may or may not strip; treat as invalid
    std::vector<std::string> const kWhitespaceSizes = {"4096 ", " 4096"};
    for (auto const& size : kWhitespaceSizes)
    {
        SCOPED_TRACE("size='" + size + "'");
        TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), size);
        EXPECT_THROW(runRoundTrip(params, 4096), std::exception);
    }
}

TEST(NuDBFactory, log_messages)
{
    // valid custom block size emits info log
    {
        TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), "8192");
        test::CaptureSink sink(beast::Severity::Info);
        beast::Journal const journal(sink);

        DummyScheduler scheduler;
        [[maybe_unused]] auto backend =
            Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);

        EXPECT_TRUE(sink.messages().contains("Using custom NuDB block size: 8192"));
    }

    // invalid block size throws with informative message
    {
        TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), "5000");
        test::CaptureSink sink(beast::Severity::Warning);
        beast::Journal const journal(sink);
        DummyScheduler scheduler;
        try
        {
            auto backend =
                Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
            FAIL() << "expected exception for invalid block size 5000";
        }
        catch (std::exception const& e)
        {
            std::string const what{e.what()};
            EXPECT_TRUE(what.contains("Invalid nudb_block_size: 5000"));
            EXPECT_TRUE(what.contains("Must be power of 2 between 4096 and 32768"));
        }
    }

    // non-numeric value throws
    {
        TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), "invalid");
        test::CaptureSink sink(beast::Severity::Warning);
        beast::Journal const journal(sink);
        DummyScheduler scheduler;
        try
        {
            auto backend =
                Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
            FAIL() << "expected exception for non-numeric block size";
        }
        catch (std::exception const& e)
        {
            std::string const what{e.what()};
            EXPECT_TRUE(what.contains("Invalid nudb_block_size value: invalid"));
        }
    }
}

TEST(NuDBFactory, power_of_two_validation)
{
    std::vector<std::pair<std::string, bool>> const kCASES = {
        {"4095", false},    // just below minimum
        {"4096", true},     // minimum valid
        {"4097", false},    // not power of 2
        {"8192", true},     // valid power of 2
        {"8193", false},    // not power of 2
        {"16384", true},    // valid power of 2
        {"32768", true},    // maximum valid
        {"32769", false},   // just above maximum
        {"65536", false}};  // power of 2 but too large

    for (auto const& [size, shouldWork] : kCASES)
    {
        SCOPED_TRACE("size=" + size + " shouldWork=" + (shouldWork ? "true" : "false"));
        TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), size);
        test::CaptureSink sink(beast::Severity::Warning);
        beast::Journal const journal(sink);
        DummyScheduler scheduler;
        try
        {
            auto backend =
                Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
            EXPECT_TRUE(shouldWork);
        }
        catch (std::exception const& e)
        {
            // A throw is only expected for sizes that should NOT work; if a
            // valid size throws, fail here instead of silently matching the
            // message below (which would mask the regression).
            EXPECT_FALSE(shouldWork);
            std::string const what{e.what()};
            EXPECT_TRUE(what.contains("Invalid nudb_block_size"));
        }
    }
}

TEST(NuDBFactory, both_constructor_variants)
{
    TempDir const tempDir;
    auto const params = makeSection(tempDir.path(), "16384");
    DummyScheduler scheduler;
    beast::Journal const journal(TestSink::instance());

    auto backend1 = Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
    EXPECT_NE(backend1, nullptr);
    ASSERT_NO_FATAL_FAILURE(runRoundTrip(params, 16384));

    // Test second constructor (with nudb::context)
    // Note: This would require access to nudb::context, which might not be
    // easily testable without more complex setup. For now, we test that
    // the factory can create backends with the first constructor.
}

TEST(NuDBFactory, configuration_parsing)
{
    // basic valid format emits success log
    {
        TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), "8192");
        test::CaptureSink sink(beast::Severity::Info);
        beast::Journal const journal(sink);
        DummyScheduler scheduler;
        [[maybe_unused]] auto backend =
            Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
        EXPECT_TRUE(sink.messages().contains("Using custom NuDB block size"));
    }

    // Test whitespace handling separately since lexical_cast behavior may vary
    std::vector<std::string> const kWhitespaceFormats = {" 8192", "8192 "};
    for (auto const& format : kWhitespaceFormats)
    {
        SCOPED_TRACE("format='" + format + "'");
        TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), format);
        test::CaptureSink sink(beast::Severity::Debug);
        beast::Journal const journal(sink);
        DummyScheduler scheduler;
        EXPECT_ANY_THROW(Manager::instance().makeBackend(params, megabytes(4), scheduler, journal));
    }
}

// Write-path measurement. NuDB holds one global mutex for the whole insert,
// so these counters plus Little's Law are the only way to separate queuing
// from service time from outside the library.

TEST(NuDBFactory, write_stats_accumulate_per_insert)
{
    TempDir const tempDir;
    auto const params = makeSection(tempDir.path());
    DummyScheduler scheduler;
    beast::Journal const journal(TestSink::instance());

    auto backend = Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
    ASSERT_TRUE(backend);
    backend->open();

    // Before any write the stats exist but are all zero, and no writer is
    // in flight.
    auto const initial = backend->getWriteStats();
    if (!initial.has_value())
        FAIL() << "nudb must report write stats";
    EXPECT_EQ(initial->insertCount, 0u);
    EXPECT_EQ(initial->insertTotalUs, 0u);
    EXPECT_EQ(initial->insertMaxUs, 0u);
    EXPECT_EQ(initial->depthSum, 0u);
    EXPECT_EQ(initial->depthSamples, 0u);
    EXPECT_EQ(initial->concurrentWriters, 0u);

    // Exactly 10 inserts must be counted as 10, and depthSum must be 10
    // because a single-threaded caller is always the only writer, so the
    // depth recorded at each insert is exactly 1.
    //
    // What this pins and what it cannot: on one thread depthSum == insertCount
    // catches an accumulator fed the wrong quantity -- fed insertCount it
    // would read 1+2+...+n, and fed the elapsed time it would read the
    // microseconds. It does NOT catch depthSum being fed a constant 1, which
    // is indistinguishable here because the real depth IS 1. That bug is the
    // one that would silently zero every derived wait time, and it is caught
    // by write_stats_measure_depth_under_real_overlap below.
    constexpr std::uint64_t kFirstBatch = 10;
    auto const batch = createPredictableBatch(kFirstBatch, 12345);
    storeBatch(*backend, batch);

    auto const after = backend->getWriteStats();
    if (!after.has_value())
        FAIL() << "nudb must report write stats after inserts";
    EXPECT_EQ(after->insertCount, kFirstBatch);
    EXPECT_EQ(after->depthSum, kFirstBatch);
    // The denominator of the published mean depth. One sample per insert, so
    // with the depth being 1 throughout, depthSum and depthSamples coincide
    // here -- which is why this pair alone cannot tell a correct depthSum from
    // a constant 1, and why the overlap test below exists.
    EXPECT_EQ(after->depthSamples, kFirstBatch);
    EXPECT_GT(after->insertTotalUs, 0u);
    EXPECT_GT(after->insertMaxUs, 0u);
    // A maximum is never below the mean, so max * n >= sum. Catches a field
    // fed the running MINIMUM: min * n <= sum, with equality only when every
    // sample is identical, so any variation at all makes the two orderings
    // exclusive. It does not discriminate when the samples happen not to
    // vary; the running-maximum property is pinned unconditionally by the
    // non-decreasing check after the second batch below.
    EXPECT_GE(after->insertMaxUs * after->insertCount, after->insertTotalUs);
    // No writer remains in flight once the calls have returned.
    EXPECT_EQ(after->concurrentWriters, 0u);

    // The counters are cumulative across calls, not per call: a second,
    // differently sized batch must add exactly its own size to both
    // totals. This is what the mean-depth denominator relies on.
    constexpr std::uint64_t kSecondBatch = 7;
    auto const more = createPredictableBatch(kSecondBatch, 999);
    storeBatch(*backend, more);

    auto const cumulative = backend->getWriteStats();
    if (!cumulative.has_value())
        FAIL() << "nudb must report write stats after a second batch";
    EXPECT_EQ(cumulative->insertCount, kFirstBatch + kSecondBatch);
    EXPECT_EQ(cumulative->depthSum, kFirstBatch + kSecondBatch);
    EXPECT_EQ(cumulative->depthSamples, kFirstBatch + kSecondBatch);
    EXPECT_EQ(cumulative->concurrentWriters, 0u);
    // A running maximum never decreases.
    EXPECT_GE(cumulative->insertMaxUs, after->insertMaxUs);
    // Nor does a running total.
    EXPECT_GE(cumulative->insertTotalUs, after->insertTotalUs);

    backend->close();
}

// Negative path: NuDB reports key_exists for a duplicate key and doInsert
// deliberately does not treat that as an error. The accounting must still
// run, and in particular the depth must come back down.
//
// What this does NOT cover, stated plainly because a comment claiming absent
// coverage is worse than none: this is not the throwing path. nudb::insert()
// sets error::key_exists and RETURNS (nudb/impl/basic_store.ipp:294, :307,
// :329), and doInsert() filters exactly that code out before it would throw
// (NuDBFactory.cpp:283), so a duplicate key takes the identical non-throwing
// control flow as a fresh key. It reaches the ScopeExit guard by the same
// route the happy path does.
//
// The throwing path -- where the guard is the only reason the depth comes
// back down -- is not reachable from a unit test: it needs nudb::insert() to
// fail with something other than key_exists (an I/O or allocation failure
// inside the library), which cannot be induced through the Backend interface
// without a fault-injection seam that does not exist. Its RAII contract is
// covered generically instead: src/tests/libxrpl/basics/scope.cpp:34-45 proves
// ScopeExit runs its function during unwinding.
TEST(NuDBFactory, write_stats_count_duplicate_key_inserts)
{
    TempDir const tempDir;
    auto const params = makeSection(tempDir.path());
    DummyScheduler scheduler;
    beast::Journal const journal(TestSink::instance());

    auto backend = Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
    ASSERT_TRUE(backend);
    backend->open();

    constexpr std::uint64_t kBatchSize = 8;
    auto const batch = createPredictableBatch(kBatchSize, 4242);
    storeBatch(*backend, batch);

    auto const first = backend->getWriteStats();
    if (!first.has_value())
        FAIL() << "nudb must report write stats";
    ASSERT_EQ(first->insertCount, kBatchSize);
    ASSERT_EQ(first->depthSum, kBatchSize);

    // Re-storing the identical batch writes nothing new, but each call is
    // still an insert attempt that entered and left the backend.
    storeBatch(*backend, batch);

    auto const second = backend->getWriteStats();
    if (!second.has_value())
        FAIL() << "nudb must report write stats after re-storing";
    // The duplicate round is counted, so a key_exists early return is not
    // skipping the accounting. Written as the first snapshot plus the batch
    // size rather than as one product, because the two sides must differ by
    // exactly the second round: an implementation that counted only the
    // rounds that stored new data would leave these equal.
    EXPECT_EQ(second->insertCount, first->insertCount + kBatchSize);
    EXPECT_EQ(second->depthSum, first->depthSum + kBatchSize);
    // Sampled at entry, so the duplicate round is sampled whether or not it
    // stores anything. An implementation that sampled only new data would
    // leave this at the first round's figure and skew the mean.
    EXPECT_EQ(second->depthSamples, first->depthSamples + kBatchSize);
    // The depth returned to zero, so the key_exists early return did not
    // leak a writer.
    EXPECT_EQ(second->concurrentWriters, 0u);
    EXPECT_EQ(backend->getWriteLoad(), 0);

    backend->close();
}

// depthSum is the L in Little's Law: mean depth L and mean insert time W give
// service time S = W / L, and the queuing time the whole diagnosis rests on is
// W - S. If depthSum were fed a constant 1 instead of the observed depth then
// L would read exactly 1.0, S would equal W, and every derived wait would read
// 0 -- a stalled write path indistinguishable from a healthy one, with nothing
// on any dashboard looking wrong.
//
// A single-threaded test cannot see that bug, because there the real depth IS
// 1. This test forces genuine overlap so the correct implementation records a
// depth above 1 and the constant-1 implementation cannot.
//
// Why the overlap is reachable and not merely hoped for: NuDB takes one global
// mutex for the entire insert, and doInsert() reads the depth BEFORE entering
// it. So while one thread is inside an insert, every other thread that reaches
// doInsert() records a depth of at least 2 and then blocks. All threads are
// released from one latch, and the round is retried until the overlap is
// observed -- so a constant-1 implementation exhausts every round and fails,
// while the real one satisfies it as soon as any two inserts overlap.
TEST(NuDBFactory, write_stats_measure_depth_under_real_overlap)
{
    TempDir const tempDir;
    auto const params = makeSection(tempDir.path());
    DummyScheduler scheduler;
    beast::Journal const journal(TestSink::instance());

    auto backend = Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
    ASSERT_TRUE(backend);
    backend->open();

    // Bounded so a genuine regression fails instead of hanging. Each round
    // runs kOverlapThreads * kOverlapPerThread inserts through one global
    // mutex, so one round already gives the correct implementation many
    // chances to overlap.
    constexpr int kMaxRounds = 20;

    std::uint64_t completedRounds = 0;
    std::optional<WriteStats> stats;

    for (auto round = 0; round < kMaxRounds; ++round)
    {
        runOverlappingInsertRound(*backend, round);
        ++completedRounds;

        stats = backend->getWriteStats();
        if (!stats.has_value())
            FAIL() << "nudb must report write stats after concurrent inserts";

        if (stats->depthSum > stats->insertCount)
            break;
    }

    if (!stats.has_value())
        FAIL() << "no round produced write stats";

    // Every insert of every round is counted exactly once. A lost increment
    // under contention fails this.
    EXPECT_EQ(stats->insertCount, completedRounds * kOverlapThreads * kOverlapPerThread);

    // A depth sample is taken when an insert starts and insertCount rises when
    // one finishes, so the two populations differ only while an insert is in
    // flight. Every thread has been joined here, so nothing is in flight and
    // they must agree exactly. That is what licenses comparing depthSum against
    // insertCount below, and it is an assertion in its own right: a depthSamples
    // stuck at zero makes the published mean depth vanish rather than read
    // wrong, because the metric is omitted when its denominator is zero.
    EXPECT_EQ(stats->depthSamples, stats->insertCount);

    // THE assertion this test exists for: strictly greater, so a depthSum fed
    // a constant 1 (or fed nothing, or fed insertCount's own delta) cannot
    // satisfy it however many rounds run.
    EXPECT_GT(stats->depthSum, stats->insertCount)
        << "depthSum must record the observed depth, not a constant 1; rounds run="
        << completedRounds;

    // Upper bound with teeth: at most kThreads writers can be inside an
    // insert at once, so no single insert can observe a depth above kThreads.
    // A missing fetch_sub in recordInsert() would let the gauge climb once
    // per insert, giving a depthSum near insertCount squared over two --
    // vastly over this bound at these counts.
    EXPECT_LE(stats->depthSum, stats->insertCount * kOverlapThreads);

    // State plus cause: the gauge is back to exactly zero, so every one of
    // the increments taken above was matched by its decrement. Exactly 0 and
    // not "small": a single leaked writer strands getWriteLoad() nonzero for
    // the life of the process, which gates history acquisition.
    EXPECT_EQ(stats->concurrentWriters, 0u);
    EXPECT_EQ(backend->getWriteLoad(), 0);

    EXPECT_GT(stats->insertMaxUs, 0u);

    backend->close();
}

TEST(NuDBFactory, write_load_reports_writer_depth)
{
    TempDir const tempDir;
    auto const params = makeSection(tempDir.path());
    DummyScheduler scheduler;
    beast::Journal const journal(TestSink::instance());

    auto backend = Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
    ASSERT_TRUE(backend);
    backend->open();

    // Idle: no writer in flight, so the load is exactly 0.
    EXPECT_EQ(backend->getWriteLoad(), 0);

    // After writes complete the depth returns to 0 rather than staying
    // elevated, because this is an instantaneous gauge and not a counter.
    // Exactly 0 and not merely small: were getWriteLoad() to return one of
    // the cumulative fields instead of the live depth -- insertCount would
    // read 5 here, insertTotalUs some microsecond total -- this fails.
    auto const batch = createPredictableBatch(5, 777);
    storeBatch(*backend, batch);
    EXPECT_EQ(backend->getWriteLoad(), 0);

    // Same value as the write-stats snapshot reports, since both read the one
    // depth atomic. Catches the two accessors drifting onto different fields.
    auto const stats = backend->getWriteStats();
    if (!stats.has_value())
        FAIL() << "nudb must report write stats";
    EXPECT_EQ(static_cast<std::uint64_t>(backend->getWriteLoad()), stats->concurrentWriters);
    // The cumulative fields did move, so the 0 above is the gauge being
    // instantaneous and not the backend having done nothing.
    EXPECT_EQ(stats->insertCount, 5u);

    // NOTE. LedgerMaster gates history acquisition on getWriteLoad() staying
    // below kMaxWriteLoadAcquire (8192), declared static constexpr inside
    // src/xrpld/app/ledger/detail/LedgerMaster.cpp and so unreachable from
    // this binary. Depth is bounded by the number of writing threads, which
    // cannot approach that figure, so the coupling is recorded here rather
    // than asserted against a literal copy of the constant that could drift.

    backend->close();
}

TEST(NuDBFactory, data_persistence)
{
    std::vector<std::string> const kBlockSizes = {"4096", "8192", "16384", "32768"};
    for (auto const& size : kBlockSizes)
    {
        SCOPED_TRACE("size=" + size);
        TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), size);
        DummyScheduler scheduler;
        beast::Journal const journal(TestSink::instance());

        // Create test data
        auto const batch = createPredictableBatch(50, 54321);

        // Store data
        {
            auto backend =
                Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
            backend->open();
            storeBatch(*backend, batch);
            backend->close();
        }

        // Retrieve data in new backend instance
        {
            auto backend =
                Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
            backend->open();
            auto const copy = fetchCopyOfBatch(*backend, batch);
            EXPECT_EQ(batch, copy);
            backend->close();
        }
    }
}

}  // namespace xrpl::node_store
