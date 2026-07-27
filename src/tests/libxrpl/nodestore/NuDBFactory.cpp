#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/temp_dir.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>

#include <gtest/gtest.h>
#include <helpers/CaptureSink.h>
#include <helpers/TestSink.h>
#include <nodestore/TestBase.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
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

}  // namespace

TEST(NuDBFactory, default_block_size)
{
    beast::TempDir const tempDir;
    auto const params = makeSection(tempDir.path());
    ASSERT_NO_FATAL_FAILURE(runRoundTrip(params, 4096));
}

TEST(NuDBFactory, valid_block_sizes)
{
    auto const kValidSizes = std::to_array<std::size_t>({4096, 8192, 16384, 32768});
    for (auto const size : kValidSizes)
    {
        SCOPED_TRACE("size=" + std::to_string(size));
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), std::to_string(size));
        ASSERT_NO_FATAL_FAILURE(runRoundTrip(params, size));
    }

    // empty value is ignored by config parser; default (4096) is used
    {
        beast::TempDir const tempDir;
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
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), size);
        EXPECT_THROW(runRoundTrip(params, 4096), std::exception);
    }

    // whitespace handling — lexical_cast may or may not strip; treat as invalid
    std::vector<std::string> const kWhitespaceSizes = {"4096 ", " 4096"};
    for (auto const& size : kWhitespaceSizes)
    {
        SCOPED_TRACE("size='" + size + "'");
        beast::TempDir const tempDir;
        auto const params = makeSection(tempDir.path(), size);
        EXPECT_THROW(runRoundTrip(params, 4096), std::exception);
    }
}

TEST(NuDBFactory, log_messages)
{
    // valid custom block size emits info log
    {
        beast::TempDir const tempDir;
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
        beast::TempDir const tempDir;
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
        beast::TempDir const tempDir;
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
        beast::TempDir const tempDir;
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
    beast::TempDir const tempDir;
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
        beast::TempDir const tempDir;
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
        beast::TempDir const tempDir;
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
    beast::TempDir const tempDir;
    auto const params = makeSection(tempDir.path());
    DummyScheduler scheduler;
    beast::Journal const journal(TestSink::instance());

    auto backend = Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
    ASSERT_TRUE(backend);
    backend->open();

    // Before any write the stats exist but are all zero, and no writer is
    // in flight.
    auto const initial = backend->getWriteStats();
    ASSERT_TRUE(initial.has_value());
    EXPECT_EQ(initial->insertCount, 0u);
    EXPECT_EQ(initial->insertTotalUs, 0u);
    EXPECT_EQ(initial->insertMaxUs, 0u);
    EXPECT_EQ(initial->depthSum, 0u);
    EXPECT_EQ(initial->concurrentWriters, 0u);

    // Exactly 10 inserts must be counted as 10, and depthSum must be 10
    // because a single-threaded caller is always the only writer, so the
    // depth recorded at each insert is exactly 1.
    constexpr std::uint64_t kFirstBatch = 10;
    auto const batch = createPredictableBatch(kFirstBatch, 12345);
    storeBatch(*backend, batch);

    auto const after = backend->getWriteStats();
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->insertCount, kFirstBatch);
    EXPECT_EQ(after->depthSum, kFirstBatch);
    EXPECT_GT(after->insertTotalUs, 0u);
    EXPECT_GT(after->insertMaxUs, 0u);
    // The largest single insert cannot exceed the sum of all of them.
    EXPECT_LE(after->insertMaxUs, after->insertTotalUs);
    // A maximum is never below the mean. This fails if the field were
    // holding the minimum, or the first or last sample, instead of the
    // running maximum.
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
    ASSERT_TRUE(cumulative.has_value());
    EXPECT_EQ(cumulative->insertCount, kFirstBatch + kSecondBatch);
    EXPECT_EQ(cumulative->depthSum, kFirstBatch + kSecondBatch);
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
TEST(NuDBFactory, write_stats_count_duplicate_key_inserts)
{
    beast::TempDir const tempDir;
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
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(first->insertCount, kBatchSize);

    // Re-storing the identical batch writes nothing new, but each call is
    // still an insert attempt that entered and left the backend.
    storeBatch(*backend, batch);

    auto const second = backend->getWriteStats();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->insertCount, kBatchSize * 2);
    EXPECT_EQ(second->depthSum, kBatchSize * 2);
    // The depth returned to zero, so the early-return error path did not
    // leak a writer.
    EXPECT_EQ(second->concurrentWriters, 0u);

    backend->close();
}

TEST(NuDBFactory, write_stats_observe_concurrent_writers)
{
    beast::TempDir const tempDir;
    auto const params = makeSection(tempDir.path());
    DummyScheduler scheduler;
    beast::Journal const journal(TestSink::instance());

    auto backend = Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
    ASSERT_TRUE(backend);
    backend->open();

    // Four threads insert distinct objects concurrently. The exact peak
    // depth is racy, but two invariants are not: every insert is counted,
    // and depthSum is at least insertCount because depth is >= 1 per
    // insert.
    constexpr std::uint64_t kThreads = 4;
    constexpr std::uint64_t kPerThread = 50;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (auto t = 0uz; t < kThreads; ++t)
    {
        threads.emplace_back([&backend, t] {
            auto const batch = createPredictableBatch(kPerThread, 1000 + t);
            for (auto const& obj : batch)
                backend->store(obj);
        });
    }
    for (auto& th : threads)
        th.join();

    auto const stats = backend->getWriteStats();
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->insertCount, kThreads * kPerThread);
    EXPECT_GE(stats->depthSum, stats->insertCount);
    EXPECT_EQ(stats->concurrentWriters, 0u);
    EXPECT_GT(stats->insertMaxUs, 0u);
    // Depth cannot exceed the number of threads that could be inside the
    // insert at once, so the mean depth is bounded by kThreads.
    EXPECT_LE(stats->depthSum, stats->insertCount * kThreads);

    backend->close();
}

TEST(NuDBFactory, write_load_reports_writer_depth)
{
    beast::TempDir const tempDir;
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
    auto const batch = createPredictableBatch(5, 777);
    storeBatch(*backend, batch);
    EXPECT_EQ(backend->getWriteLoad(), 0);

    // The value must stay far below the history-acquisition cutoff that
    // LedgerMaster applies (kMaxWriteLoadAcquire), or history acquisition
    // would silently stop. Depth is bounded by the writing threads.
    constexpr int kMaxWriteLoadAcquire = 8192;
    EXPECT_LT(backend->getWriteLoad(), kMaxWriteLoadAcquire);

    backend->close();
}

TEST(NuDBFactory, data_persistence)
{
    std::vector<std::string> const kBlockSizes = {"4096", "8192", "16384", "32768"};
    for (auto const& size : kBlockSizes)
    {
        SCOPED_TRACE("size=" + size);
        beast::TempDir const tempDir;
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
