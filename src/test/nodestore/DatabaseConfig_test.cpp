#include <test/jtx/CheckMessageLogs.h>
#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpld/core/Config.h>
#ifdef XRPL_ENABLE_TELEMETRY
// The four nodestore_state gauge helpers under test, plus the counter type one
// of them reads. Both live in xrpld and are only declared in a
// telemetry-enabled build, so the include is guarded like its uses below.
#include <xrpld/app/ledger/AcquireStats.h>
#include <xrpld/telemetry/MetricsRegistry.h>
#endif

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/rngfill.h>
#include <xrpl/beast/utility/temp_dir.h>
#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Types.h>
#include <xrpl/nodestore/detail/DatabaseRotatingImp.h>
#include <xrpl/rdb/DatabaseCon.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl::node_store {

class DatabaseConfig_test : public beast::unit_test::Suite
{
private:
    /**
     * Journal for backends and databases created by this suite.
     *
     * The nodestore suites that used to share these helpers moved to GTest,
     * taking their common base with them. This suite stayed on Beast, so it
     * keeps its own copy of the few pieces it needs. SuiteJournal rather than
     * a bare beast::Journal, which has no default constructor; it converts
     * implicitly where a journal is expected.
     */
    test::SuiteJournal journal_{"DatabaseConfig_test", *this};

    /**
     * Build a batch of node objects that is the same for a given seed.
     *
     * Payload sizes and contents come from a seeded generator, so two runs
     * with one seed produce identical objects and a test can store one batch
     * and look for exactly those hashes later.
     *
     * @param numObjects How many objects to create.
     * @param seed Seed for the generator.
     * @return The batch, in creation order.
     */
    static Batch
    createPredictableBatch(int numObjects, std::uint64_t seed)
    {
        Batch batch;
        batch.reserve(numObjects);

        beast::xor_shift_engine rng(seed);

        for (int i = 0; i < numObjects; ++i)
        {
            uint256 hash;
            beast::rngfill(hash.begin(), hash.size(), rng);

            Blob blob(randInt(rng, std::size_t{1}, std::size_t{2000}));
            beast::rngfill(blob.data(), blob.size(), rng);

            batch.emplace_back(
                NodeObject::createObject(NodeObjectType::Ledger, std::move(blob), hash));
        }

        return batch;
    }

    /**
     * Store every object of a batch in a database.
     *
     * @param db Destination database.
     * @param batch Objects to store.
     */
    static void
    storeBatch(Database& db, Batch const& batch)
    {
        for (auto const& object : batch)
        {
            Blob data(object->getData());
            db.store(object->getType(), std::move(data), object->getHash(), db.earliestLedgerSeq());
        }
    }

    /**
     * Store every object of a batch directly in a backend.
     *
     * @param backend Destination backend.
     * @param batch Objects to store.
     */
    static void
    storeBatch(Backend& backend, Batch const& batch)
    {
        for (auto const& object : batch)
            backend.store(object);
    }

    /**
     * A DummyScheduler that also totals what the fetch reports carried.
     *
     * Database::fetchNodeObject() measures each fetch once and uses that one
     * value for both its internal accumulator and the report it hands the
     * scheduler, so the report total is an independent reading of the same
     * measurement. Comparing the two pins the accumulator without asserting
     * that a fetch took any particular amount of time -- which is a statement
     * about the machine, not the code, and fails on a host fast enough to
     * serve every read in under a microsecond.
     *
     * @note Counters are atomic because Scheduler is called from the
     *       nodestore's read threads in production. These tests fetch
     *       synchronously on one thread, so the totals are still exact.
     */
    struct CountingScheduler : DummyScheduler
    {
        /**
         * Sum of the durations carried by every fetch report received.
         */
        std::atomic<std::uint64_t> reportedFetchUs{0};

        /**
         * Fetch reports received, however long each one measured.
         */
        std::atomic<std::uint64_t> fetchReports{0};

        void
        onFetch(FetchReport const& report) override
        {
            reportedFetchUs += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(report.elapsed).count());
            ++fetchReports;
        }
    };

    /**
     * Assert the read accumulator holds exactly what the fetch reports carried.
     *
     * Deliberately not `getFetchDurationUs() > 0`: an individual read served
     * from NuDB's in-memory buckets can genuinely measure under one
     * microsecond and truncate to zero, so on a fast enough host every read
     * truncates and the total stays at zero with nothing wrong. That makes
     * `> 0` an assertion about the machine rather than about the code, and it
     * is what failed on the macOS runner.
     *
     * The equality below is machine-independent and strictly stronger: each
     * fetch is measured once and that one value feeds both the accumulator
     * and the report, so an accumulator that dropped a fetch, double-counted
     * one, or reported the write member instead would break the equality
     * however fast the host is.
     *
     * @param db Database whose read accumulator is checked.
     * @param scheduler Scheduler that received the reports for @p db.
     * @param expectedReports Fetches that must have been reported.
     */
    void
    expectFetchDurationMatchesReports(
        Database const& db,
        CountingScheduler const& scheduler,
        std::uint64_t expectedReports)
    {
        BEAST_EXPECT(scheduler.fetchReports.load() == expectedReports);
        BEAST_EXPECT(db.getFetchDurationUs() == scheduler.reportedFetchUs.load());
    }

public:
    void
    testConfig()
    {
        testcase("Config");

        using namespace xrpl::test;
        using namespace xrpl::test::jtx;

        auto const integrityWarning =
            "reducing the data integrity guarantees from the "
            "default [sqlite] behavior is not recommended for "
            "nodes storing large amounts of history, because of the "
            "difficulty inherent in rebuilding corrupted data.";
        {
            // defaults
            Env env(*this);

            auto const s = setupDatabaseCon(env.app().config());

            if (BEAST_EXPECT(s.globalPragma->size() == 3))
            {
                BEAST_EXPECT(s.globalPragma->at(0) == "PRAGMA journal_mode=wal;");
                BEAST_EXPECT(s.globalPragma->at(1) == "PRAGMA synchronous=normal;");
                BEAST_EXPECT(s.globalPragma->at(2) == "PRAGMA temp_store=file;");
            }
        }
        {
            // High safety level
            DatabaseCon::Setup::globalPragma.reset();

            bool found = false;
            Env env = [&]() {
                auto p = test::jtx::envconfig();
                {
                    auto& section = p->section("sqlite");
                    section.set("safety_level", "high");
                }
                p->ledgerHistory = 100'000'000;

                return Env(
                    *this,
                    std::move(p),
                    std::make_unique<CheckMessageLogs>(integrityWarning, &found),
                    beast::Severity::Warning);
            }();

            BEAST_EXPECT(!found);
            auto const s = setupDatabaseCon(env.app().config());
            if (BEAST_EXPECT(s.globalPragma->size() == 3))
            {
                BEAST_EXPECT(s.globalPragma->at(0) == "PRAGMA journal_mode=wal;");
                BEAST_EXPECT(s.globalPragma->at(1) == "PRAGMA synchronous=normal;");
                BEAST_EXPECT(s.globalPragma->at(2) == "PRAGMA temp_store=file;");
            }
        }
        {
            // Low safety level
            DatabaseCon::Setup::globalPragma.reset();

            bool found = false;
            Env env = [&]() {
                auto p = test::jtx::envconfig();
                {
                    auto& section = p->section("sqlite");
                    section.set("safety_level", "low");
                }
                p->ledgerHistory = 100'000'000;

                return Env(
                    *this,
                    std::move(p),
                    std::make_unique<CheckMessageLogs>(integrityWarning, &found),
                    beast::Severity::Warning);
            }();

            BEAST_EXPECT(found);
            auto const s = setupDatabaseCon(env.app().config());
            if (BEAST_EXPECT(s.globalPragma->size() == 3))
            {
                BEAST_EXPECT(s.globalPragma->at(0) == "PRAGMA journal_mode=memory;");
                BEAST_EXPECT(s.globalPragma->at(1) == "PRAGMA synchronous=off;");
                BEAST_EXPECT(s.globalPragma->at(2) == "PRAGMA temp_store=memory;");
            }
        }
        {
            // Override individual settings
            DatabaseCon::Setup::globalPragma.reset();

            bool found = false;
            Env env = [&]() {
                auto p = test::jtx::envconfig();
                {
                    auto& section = p->section("sqlite");
                    section.set("journal_mode", "off");
                    section.set("synchronous", "extra");
                    section.set("temp_store", "default");
                }

                return Env(
                    *this,
                    std::move(p),
                    std::make_unique<CheckMessageLogs>(integrityWarning, &found),
                    beast::Severity::Warning);
            }();

            // No warning, even though higher risk settings were used because
            // ledgerHistory is small
            BEAST_EXPECT(!found);
            auto const s = setupDatabaseCon(env.app().config());
            if (BEAST_EXPECT(s.globalPragma->size() == 3))
            {
                BEAST_EXPECT(s.globalPragma->at(0) == "PRAGMA journal_mode=off;");
                BEAST_EXPECT(s.globalPragma->at(1) == "PRAGMA synchronous=extra;");
                BEAST_EXPECT(s.globalPragma->at(2) == "PRAGMA temp_store=default;");
            }
        }
        {
            // Override individual settings with large history
            DatabaseCon::Setup::globalPragma.reset();

            bool found = false;
            Env env = [&]() {
                auto p = test::jtx::envconfig();
                {
                    auto& section = p->section("sqlite");
                    section.set("journal_mode", "off");
                    section.set("synchronous", "extra");
                    section.set("temp_store", "default");
                }
                p->ledgerHistory = 50'000'000;

                return Env(
                    *this,
                    std::move(p),
                    std::make_unique<CheckMessageLogs>(integrityWarning, &found),
                    beast::Severity::Warning);
            }();

            // No warning, even though higher risk settings were used because
            // ledgerHistory is small
            BEAST_EXPECT(found);
            auto const s = setupDatabaseCon(env.app().config());
            if (BEAST_EXPECT(s.globalPragma->size() == 3))
            {
                BEAST_EXPECT(s.globalPragma->at(0) == "PRAGMA journal_mode=off;");
                BEAST_EXPECT(s.globalPragma->at(1) == "PRAGMA synchronous=extra;");
                BEAST_EXPECT(s.globalPragma->at(2) == "PRAGMA temp_store=default;");
            }
        }
        {
            // Error: Mix safety_level and individual settings
            DatabaseCon::Setup::globalPragma.reset();
            auto const expected =
                "Failed to initialize SQL databases: "
                "Configuration file may not define both \"safety_level\" and "
                "\"journal_mode\"";
            bool found = false;

            auto p = test::jtx::envconfig();
            {
                auto& section = p->section("sqlite");
                section.set("safety_level", "low");
                section.set("journal_mode", "off");
                section.set("synchronous", "extra");
                section.set("temp_store", "default");
            }

            try
            {
                Env const env(
                    *this,
                    std::move(p),
                    std::make_unique<CheckMessageLogs>(expected, &found),
                    beast::Severity::Warning);
                fail();
            }
            catch (...)
            {
                BEAST_EXPECT(found);
            }
        }
        {
            // Error: Mix safety_level and one setting (gotta catch 'em all)
            DatabaseCon::Setup::globalPragma.reset();
            auto const expected =
                "Failed to initialize SQL databases: Configuration file may "
                "not define both \"safety_level\" and \"journal_mode\"";
            bool found = false;

            auto p = test::jtx::envconfig();
            {
                auto& section = p->section("sqlite");
                section.set("safety_level", "high");
                section.set("journal_mode", "off");
            }

            try
            {
                Env const env(
                    *this,
                    std::move(p),
                    std::make_unique<CheckMessageLogs>(expected, &found),
                    beast::Severity::Warning);
                fail();
            }
            catch (...)
            {
                BEAST_EXPECT(found);
            }
        }
        {
            // Error: Mix safety_level and one setting (gotta catch 'em all)
            DatabaseCon::Setup::globalPragma.reset();
            auto const expected =
                "Failed to initialize SQL databases: Configuration file may "
                "not define both \"safety_level\" and \"synchronous\"";
            bool found = false;

            auto p = test::jtx::envconfig();
            {
                auto& section = p->section("sqlite");
                section.set("safety_level", "low");
                section.set("synchronous", "extra");
            }

            try
            {
                Env const env(
                    *this,
                    std::move(p),
                    std::make_unique<CheckMessageLogs>(expected, &found),
                    beast::Severity::Warning);
                fail();
            }
            catch (...)
            {
                BEAST_EXPECT(found);
            }
        }
        {
            // Error: Mix safety_level and one setting (gotta catch 'em all)
            DatabaseCon::Setup::globalPragma.reset();
            auto const expected =
                "Failed to initialize SQL databases: Configuration file may "
                "not define both \"safety_level\" and \"temp_store\"";
            bool found = false;

            auto p = test::jtx::envconfig();
            {
                auto& section = p->section("sqlite");
                section.set("safety_level", "high");
                section.set("temp_store", "default");
            }

            try
            {
                Env const env(
                    *this,
                    std::move(p),
                    std::make_unique<CheckMessageLogs>(expected, &found),
                    beast::Severity::Warning);
                fail();
            }
            catch (...)
            {
                BEAST_EXPECT(found);
            }
        }
        {
            // Error: Invalid value
            DatabaseCon::Setup::globalPragma.reset();
            auto const expected =
                "Failed to initialize SQL databases: Invalid safety_level "
                "value: slow";
            bool found = false;

            auto p = test::jtx::envconfig();
            {
                auto& section = p->section("sqlite");
                section.set("safety_level", "slow");
            }

            try
            {
                Env const env(
                    *this,
                    std::move(p),
                    std::make_unique<CheckMessageLogs>(expected, &found),
                    beast::Severity::Warning);
                fail();
            }
            catch (...)
            {
                BEAST_EXPECT(found);
            }
        }
        {
            // Error: Invalid value
            DatabaseCon::Setup::globalPragma.reset();
            auto const expected =
                "Failed to initialize SQL databases: Invalid journal_mode "
                "value: fast";
            bool found = false;

            auto p = test::jtx::envconfig();
            {
                auto& section = p->section("sqlite");
                section.set("journal_mode", "fast");
            }

            try
            {
                Env const env(
                    *this,
                    std::move(p),
                    std::make_unique<CheckMessageLogs>(expected, &found),
                    beast::Severity::Warning);
                fail();
            }
            catch (...)
            {
                BEAST_EXPECT(found);
            }
        }
        {
            // Error: Invalid value
            DatabaseCon::Setup::globalPragma.reset();
            auto const expected =
                "Failed to initialize SQL databases: Invalid synchronous "
                "value: instant";
            bool found = false;

            auto p = test::jtx::envconfig();
            {
                auto& section = p->section("sqlite");
                section.set("synchronous", "instant");
            }

            try
            {
                Env const env(
                    *this,
                    std::move(p),
                    std::make_unique<CheckMessageLogs>(expected, &found),
                    beast::Severity::Warning);
                fail();
            }
            catch (...)
            {
                BEAST_EXPECT(found);
            }
        }
        {
            // Error: Invalid value
            DatabaseCon::Setup::globalPragma.reset();
            auto const expected =
                "Failed to initialize SQL databases: Invalid temp_store "
                "value: network";
            bool found = false;

            auto p = test::jtx::envconfig();
            {
                auto& section = p->section("sqlite");
                section.set("temp_store", "network");
            }

            try
            {
                Env const env(
                    *this,
                    std::move(p),
                    std::make_unique<CheckMessageLogs>(expected, &found),
                    beast::Severity::Warning);
                fail();
            }
            catch (...)
            {
                BEAST_EXPECT(found);
            }
        }
        {
            // N/A: Default values
            Env env(*this);
            auto const s = setupDatabaseCon(env.app().config());
            if (BEAST_EXPECT(s.txPragma.size() == 4))
            {
                BEAST_EXPECT(s.txPragma.at(0) == "PRAGMA page_size=4096;");
                BEAST_EXPECT(s.txPragma.at(1) == "PRAGMA journal_size_limit=1582080;");
                BEAST_EXPECT(s.txPragma.at(2) == "PRAGMA max_page_count=4294967294;");
                BEAST_EXPECT(s.txPragma.at(3) == "PRAGMA mmap_size=17179869184;");
            }
        }
        {
            // Success: Valid values
            Env env = [&]() {
                auto p = test::jtx::envconfig();
                {
                    auto& section = p->section("sqlite");
                    section.set("page_size", "512");
                    section.set("journal_size_limit", "2582080");
                }
                return Env(*this, std::move(p));
            }();
            auto const s = setupDatabaseCon(env.app().config());
            if (BEAST_EXPECT(s.txPragma.size() == 4))
            {
                BEAST_EXPECT(s.txPragma.at(0) == "PRAGMA page_size=512;");
                BEAST_EXPECT(s.txPragma.at(1) == "PRAGMA journal_size_limit=2582080;");
                BEAST_EXPECT(s.txPragma.at(2) == "PRAGMA max_page_count=4294967294;");
                BEAST_EXPECT(s.txPragma.at(3) == "PRAGMA mmap_size=17179869184;");
            }
        }
        {
            // Error: Invalid values
            auto const expected = "Invalid page_size. Must be between 512 and 65536.";
            bool found = false;
            auto p = test::jtx::envconfig();
            {
                auto& section = p->section("sqlite");
                section.set("page_size", "256");
            }
            try
            {
                Env const env(
                    *this,
                    std::move(p),
                    std::make_unique<CheckMessageLogs>(expected, &found),
                    beast::Severity::Warning);
                fail();
            }
            catch (...)
            {
                BEAST_EXPECT(found);
            }
        }
        {
            // Error: Invalid values
            auto const expected = "Invalid page_size. Must be between 512 and 65536.";
            bool found = false;
            auto p = test::jtx::envconfig();
            {
                auto& section = p->section("sqlite");
                section.set("page_size", "131072");
            }
            try
            {
                Env const env(
                    *this,
                    std::move(p),
                    std::make_unique<CheckMessageLogs>(expected, &found),
                    beast::Severity::Warning);
                fail();
            }
            catch (...)
            {
                BEAST_EXPECT(found);
            }
        }
        {
            // Error: Invalid values
            auto const expected = "Invalid page_size. Must be a power of 2.";
            bool found = false;
            auto p = test::jtx::envconfig();
            {
                auto& section = p->section("sqlite");
                section.set("page_size", "513");
            }
            try
            {
                Env const env(
                    *this,
                    std::move(p),
                    std::make_unique<CheckMessageLogs>(expected, &found),
                    beast::Severity::Warning);
                fail();
            }
            catch (...)
            {
                BEAST_EXPECT(found);
            }
        }
    }

    //--------------------------------------------------------------------------

    /**
     * Verify the fetch counters are 64-bit and accumulate exact values.
     *
     * These counters run for the whole process lifetime. A 32-bit byte
     * counter wraps in under an hour at production read rates, which
     * silently corrupts any ratio built from it.
     */
    void
    testCounterWidths()
    {
        testcase("Fetch counters are 64-bit");

        // The accessors must not narrow the widened members back to 32 bits.
        static_assert(
            std::is_same_v<
                decltype(std::declval<Database const&>().getFetchTotalCount()),
                std::uint64_t>,
            "getFetchTotalCount must be 64-bit");
        static_assert(
            std::is_same_v<
                decltype(std::declval<Database const&>().getFetchHitCount()),
                std::uint64_t>,
            "getFetchHitCount must be 64-bit");
        static_assert(
            std::is_same_v<decltype(std::declval<Database const&>().getFetchSize()), std::uint64_t>,
            "getFetchSize must be 64-bit");

        DummyScheduler scheduler;

        beast::TempDir const nodeDb;
        Section nodeParams;
        nodeParams.set(Keys::kType, "memory");
        nodeParams.set(Keys::kPath, nodeDb.path());

        // No cache_size/cache_age is set, so DatabaseNodeImp builds no cache
        // and every fetch reaches the backend. That makes the counts exact.
        std::unique_ptr<Database> db =
            Manager::instance().makeDatabase(megabytes(4), scheduler, 2, nodeParams, journal_);

        // A fresh database has counted nothing.
        BEAST_EXPECT(db->getFetchTotalCount() == 0);
        BEAST_EXPECT(db->getFetchHitCount() == 0);
        BEAST_EXPECT(db->getFetchSize() == 0);

        // Store a small batch and record the exact payload byte total.
        constexpr std::uint64_t kNumStored = 8;
        auto const stored = createPredictableBatch(static_cast<int>(kNumStored), 12345);
        BEAST_EXPECT(stored.size() == kNumStored);
        storeBatch(*db, stored);

        std::uint64_t expectedBytes = 0;
        for (auto const& object : stored)
            expectedBytes += object->getData().size();

        // Storing must not move the fetch counters.
        BEAST_EXPECT(db->getFetchTotalCount() == 0);
        BEAST_EXPECT(db->getFetchHitCount() == 0);
        BEAST_EXPECT(db->getFetchSize() == 0);

        // Positive path: every fetch finds its object.
        for (auto const& object : stored)
            BEAST_EXPECT(db->fetchNodeObject(object->getHash(), 0) != nullptr);

        BEAST_EXPECT(db->getFetchTotalCount() == kNumStored);
        BEAST_EXPECT(db->getFetchHitCount() == kNumStored);
        BEAST_EXPECT(db->getFetchSize() == expectedBytes);

        // Negative path: hashes that were never stored count as attempts but
        // not as hits, and add no bytes.
        constexpr std::uint64_t kNumMissing = 5;
        auto const missing = createPredictableBatch(static_cast<int>(kNumMissing), 999);
        BEAST_EXPECT(missing.size() == kNumMissing);
        for (auto const& object : missing)
            BEAST_EXPECT(db->fetchNodeObject(object->getHash(), 0) == nullptr);

        BEAST_EXPECT(db->getFetchTotalCount() == kNumStored + kNumMissing);
        BEAST_EXPECT(db->getFetchHitCount() == kNumStored);
        BEAST_EXPECT(db->getFetchSize() == expectedBytes);
    }

    //--------------------------------------------------------------------------

    /**
     * Verify the fetch and store duration accumulators are readable directly.
     *
     * Telemetry needs the mean backend latency, which is the cumulative
     * duration divided by the matching operation count. Both accumulators
     * must therefore be readable without a JSON round trip, must be zero on
     * a fresh database, and must be independent of each other: writes may
     * not move the read total and reads may not move the write total.
     */
    void
    testDurationAccessors()
    {
        testcase("Fetch and store duration accessors");

        CountingScheduler scheduler;

        beast::TempDir const nodeDb;
        Section nodeParams;
        nodeParams.set(Keys::kType, "nudb");
        nodeParams.set(Keys::kPath, nodeDb.path());

        // No cache_size/cache_age is set, so DatabaseNodeImp builds no cache
        // and every fetch reaches the backend. That makes the counts exact.
        std::unique_ptr<Database> db =
            Manager::instance().makeDatabase(megabytes(4), scheduler, 2, nodeParams, journal_);
        if (!BEAST_EXPECT(db))
            return;

        // A fresh database has done no work, so every accumulator reads zero.
        BEAST_EXPECT(db->getStoreCount() == 0);
        BEAST_EXPECT(db->getStoreDurationUs() == 0);
        BEAST_EXPECT(db->getFetchTotalCount() == 0);
        BEAST_EXPECT(db->getFetchHitCount() == 0);
        BEAST_EXPECT(db->getFetchDurationUs() == 0);

        // Writes must advance the write count exactly and the write duration
        // past zero: the backend inserts take microseconds of real work.
        constexpr std::uint64_t kNumStored = 32;
        auto const stored = createPredictableBatch(static_cast<int>(kNumStored), 4321);
        BEAST_EXPECT(stored.size() == kNumStored);

        auto const writesBegan = std::chrono::steady_clock::now();
        storeBatch(*db, stored);
        auto const wallClockUs =
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                           std::chrono::steady_clock::now() - writesBegan)
                                           .count());

        BEAST_EXPECT(db->getStoreCount() == kNumStored);
        BEAST_EXPECT(db->getStoreDurationUs() > 0);

        // Bounded above by the wall-clock span of the loop that produced it.
        // The accumulator only ever sums the backend calls made inside that
        // span, so it cannot exceed it. Without this bound the `> 0` above
        // would also pass for an accumulator adding a fixed constant per
        // insert instead of the measured time.
        BEAST_EXPECT(db->getStoreDurationUs() <= wallClockUs);

        // Writes must leave the read accumulator alone. This also proves the
        // read accessor does not report the write member.
        BEAST_EXPECT(db->getFetchDurationUs() == 0);

        auto const storeDurationAfterWrites = db->getStoreDurationUs();

        // Positive path: every fetch finds its object, so the read counters
        // and the read duration all advance.
        for (auto const& object : stored)
            BEAST_EXPECT(db->fetchNodeObject(object->getHash(), 0) != nullptr);

        BEAST_EXPECT(db->getFetchTotalCount() == kNumStored);
        BEAST_EXPECT(db->getFetchHitCount() == kNumStored);
        expectFetchDurationMatchesReports(*db, scheduler, kNumStored);

        // Reads must leave the write accumulator alone. This also proves the
        // write accessor does not report the read member.
        BEAST_EXPECT(db->getStoreDurationUs() == storeDurationAfterWrites);

        // Negative path: a miss counts as a fetch but not as a hit, and it
        // performs no write, so the write accumulator still cannot move.
        constexpr std::uint64_t kNumMissing = 8;
        auto const missing = createPredictableBatch(static_cast<int>(kNumMissing), 999);
        BEAST_EXPECT(missing.size() == kNumMissing);
        for (auto const& object : missing)
            BEAST_EXPECT(db->fetchNodeObject(object->getHash(), 0) == nullptr);

        BEAST_EXPECT(db->getFetchTotalCount() == kNumStored + kNumMissing);
        BEAST_EXPECT(db->getFetchHitCount() == kNumStored);
        BEAST_EXPECT(db->getStoreCount() == kNumStored);
        BEAST_EXPECT(db->getStoreDurationUs() == storeDurationAfterWrites);
    }

    //--------------------------------------------------------------------------

    /**
     * Build a rotating nudb database over two fresh directories.
     *
     * @param scheduler Scheduler the database keeps a reference to; it must
     *                  outlive the returned database.
     * @param writableDir Directory for the writable backend.
     * @param archiveDir Directory for the archive backend.
     * @return The rotating database, or nullptr if either backend failed.
     */
    std::unique_ptr<DatabaseRotatingImp>
    makeRotatingDatabase(
        Scheduler& scheduler,
        beast::TempDir const& writableDir,
        beast::TempDir const& archiveDir)
    {
        Section writableParams;
        writableParams.set(Keys::kType, "nudb");
        writableParams.set(Keys::kPath, writableDir.path());

        Section archiveParams;
        archiveParams.set(Keys::kType, "nudb");
        archiveParams.set(Keys::kPath, archiveDir.path());

        std::shared_ptr<Backend> writableBackend =
            Manager::instance().makeBackend(writableParams, megabytes(4), scheduler, journal_);
        std::shared_ptr<Backend> archiveBackend =
            Manager::instance().makeBackend(archiveParams, megabytes(4), scheduler, journal_);
        if (!writableBackend || !archiveBackend)
            return nullptr;
        writableBackend->open();
        archiveBackend->open();

        return std::make_unique<DatabaseRotatingImp>(
            scheduler,
            2,
            std::move(writableBackend),
            std::move(archiveBackend),
            writableParams,
            journal_);
    }

    /**
     * Verify the rotating database records its write duration too.
     *
     * DatabaseRotatingImp has its own store path, separate from
     * DatabaseNodeImp, and it is the path a node with online delete runs.
     * A getter that only the non-rotating path feeds would read zero for
     * the whole lifetime of such a node.
     */
    void
    testRotatingDurationAccessors()
    {
        testcase("Rotating store duration accessors");

        CountingScheduler scheduler;

        beast::TempDir const writableDir;
        beast::TempDir const archiveDir;

        auto rotating = makeRotatingDatabase(scheduler, writableDir, archiveDir);
        if (!BEAST_EXPECT(rotating))
            return;

        // The private fetchNodeObject override hides the public base overload,
        // so exercise the rotating store through the Database interface, which
        // is also how production callers reach it.
        Database& db = *rotating;

        // A fresh rotating database has done no work either.
        BEAST_EXPECT(db.getStoreCount() == 0);
        BEAST_EXPECT(db.getStoreDurationUs() == 0);
        BEAST_EXPECT(db.getFetchTotalCount() == 0);
        BEAST_EXPECT(db.getFetchDurationUs() == 0);

        constexpr std::uint64_t kNumStored = 32;
        auto const stored = createPredictableBatch(static_cast<int>(kNumStored), 8642);
        BEAST_EXPECT(stored.size() == kNumStored);
        storeBatch(db, stored);

        BEAST_EXPECT(db.getStoreCount() == kNumStored);
        BEAST_EXPECT(db.getStoreDurationUs() > 0);
        BEAST_EXPECT(db.getFetchDurationUs() == 0);

        auto const storeDurationAfterWrites = db.getStoreDurationUs();

        // Every object is in the writable backend, so the archive is never
        // consulted and no copy-forward write happens on these reads.
        for (auto const& object : stored)
            BEAST_EXPECT(db.fetchNodeObject(object->getHash(), 0) != nullptr);

        BEAST_EXPECT(db.getFetchTotalCount() == kNumStored);
        BEAST_EXPECT(db.getFetchHitCount() == kNumStored);
        expectFetchDurationMatchesReports(db, scheduler, kNumStored);
        BEAST_EXPECT(db.getStoreCount() == kNumStored);
        BEAST_EXPECT(db.getStoreDurationUs() == storeDurationAfterWrites);

        // Negative path: a hash in neither backend misses both, so it counts
        // as a fetch, not as a hit, and triggers no copy-forward write.
        constexpr std::uint64_t kNumMissing = 8;
        auto const missing = createPredictableBatch(static_cast<int>(kNumMissing), 1357);
        BEAST_EXPECT(missing.size() == kNumMissing);
        for (auto const& object : missing)
            BEAST_EXPECT(db.fetchNodeObject(object->getHash(), 0) == nullptr);

        BEAST_EXPECT(db.getFetchTotalCount() == kNumStored + kNumMissing);
        BEAST_EXPECT(db.getFetchHitCount() == kNumStored);
        BEAST_EXPECT(db.getStoreCount() == kNumStored);
        BEAST_EXPECT(db.getStoreDurationUs() == storeDurationAfterWrites);
    }

    //--------------------------------------------------------------------------

#ifdef XRPL_ENABLE_TELEMETRY

    /**
     * Recording sink for the nodestore_state gauge helpers.
     *
     * The helpers take an ObserveFn rather than the OTel observer result
     * precisely so a test can hand them somewhere else to put a name and a
     * number. Emissions are kept in order, not merged into a map, so a label
     * published twice is visible instead of silently overwriting itself.
     */
    struct MetricSink
    {
        /**
         * Every (metric label, value) pair published, in emission order.
         */
        std::vector<std::pair<std::string, std::int64_t>> emitted;

        /**
         * Return a sink callable that appends into @ref emitted.
         */
        telemetry::MetricsRegistry::ObserveFn
        fn()
        {
            return
                [this](char const* name, std::int64_t value) { emitted.emplace_back(name, value); };
        }

        /**
         * Return every published label, sorted, for set comparison.
         */
        [[nodiscard]] std::vector<std::string>
        names() const
        {
            std::vector<std::string> out;
            out.reserve(emitted.size());
            for (auto const& entry : emitted)
                out.push_back(entry.first);
            std::ranges::sort(out);
            return out;
        }

        /**
         * Return the value published for @p name, or std::nullopt when the
         * label was not published at all.
         *
         * Absence and zero must stay distinguishable: a mean over no samples
         * is deliberately omitted, while a counter at zero is published.
         *
         * @param name Metric label to look up.
         */
        [[nodiscard]] std::optional<std::int64_t>
        value(std::string const& name) const
        {
            auto const it = std::ranges::find_if(
                emitted, [&name](auto const& entry) { return entry.first == name; });
            if (it == emitted.end())
                return std::nullopt;
            return it->second;
        }
    };

    /**
     * Build a nudb database with a non-default read-thread count and bundle.
     *
     * Both are set away from their defaults so the read-queue labels pin real
     * forwarding rather than agreeing with a default by accident.
     *
     * @param dir Directory the store lives in.
     * @param scheduler Scheduler the database keeps a reference to; it must
     *                  outlive the returned database.
     * @param readThreads Read threads to request.
     * @return The database, or nullptr on failure.
     */
    std::unique_ptr<Database>
    makeMeasuredDatabase(beast::TempDir const& dir, Scheduler& scheduler, int readThreads)
    {
        Section params;
        params.set(Keys::kType, "nudb");
        params.set(Keys::kPath, dir.path());
        params.set(Keys::kRqBundle, "7");

        return Manager::instance().makeDatabase(
            megabytes(4), scheduler, readThreads, params, journal_);
    }

    /**
     * Verify the exact `metric` label set observeNodeStoreTotals() publishes,
     * and that each derived mean is OMITTED rather than reported as zero
     * before there is anything to average.
     *
     * The 22 label values on this gauge are the change's entire user-visible
     * surface. A one-character typo in any of them produces a silently
     * disjoint Prometheus series: the metric still exports, the dashboard
     * panel goes blank, and nothing else fails.
     */
    void
    testNodeStoreTotalLabels()
    {
        testcase("nodestore_state totals labels");

        DummyScheduler scheduler;
        beast::TempDir const nodeDb;
        Section nodeParams;
        nodeParams.set(Keys::kType, "nudb");
        nodeParams.set(Keys::kPath, nodeDb.path());

        std::unique_ptr<Database> db =
            Manager::instance().makeDatabase(megabytes(4), scheduler, 2, nodeParams, journal_);
        if (!BEAST_EXPECT(db))
            return;

        // Fresh store: the eight unconditional labels are published, each at
        // exactly zero, and NEITHER mean appears.
        MetricSink fresh;
        telemetry::MetricsRegistry::observeNodeStoreTotals(*db, fresh.fn());

        std::vector<std::string> const kFreshLabels{
            "node_read_bytes",
            "node_reads_duration_us",
            "node_reads_hit",
            "node_reads_total",
            "node_writes",
            "node_writes_duration_us",
            "node_written_bytes",
            "write_load"};
        BEAST_EXPECT(fresh.names() == kFreshLabels);
        // No label published twice; a map-based sink would have hidden that.
        BEAST_EXPECT(fresh.emitted.size() == kFreshLabels.size());

        for (auto const& label : kFreshLabels)
            BEAST_EXPECT(fresh.value(label) == std::int64_t{0});

        // THE omission assertion. A refactor to `mean.value_or(0)` publishes
        // these as a believable flat zero on a latency axis; absence is the
        // only honest answer before anything has been read or written.
        BEAST_EXPECT(!fresh.value("read_mean_us").has_value());
        BEAST_EXPECT(!fresh.value("write_mean_us").has_value());

        testNodeStoreMeanLabels(*db);
    }

    /**
     * Verify both derived means appear once there is work to average, and
     * that each is the quotient of its OWN published numerator and
     * denominator.
     *
     * @param db Database to drive; must be freshly created.
     */
    void
    testNodeStoreMeanLabels(Database& db)
    {
        // Reads and writes are given deliberately different counts, so a mean
        // wired to the wrong accessor pair -- the adjacent-call-site copy-paste
        // risk -- cannot land on the right number.
        constexpr std::uint64_t kNumStored = 32;
        constexpr std::uint64_t kNumMissing = 8;
        auto const stored = createPredictableBatch(static_cast<int>(kNumStored), 24680);
        storeBatch(db, stored);

        // Fetch each stored object twice, plus a batch of misses, so the read
        // count is 2*32 + 8 = 72 against a write count of 32.
        for (int pass = 0; pass < 2; ++pass)
        {
            for (auto const& object : stored)
                BEAST_EXPECT(db.fetchNodeObject(object->getHash(), 0) != nullptr);
        }
        auto const missing = createPredictableBatch(static_cast<int>(kNumMissing), 13579);
        for (auto const& object : missing)
            BEAST_EXPECT(db.fetchNodeObject(object->getHash(), 0) == nullptr);

        MetricSink busy;
        telemetry::MetricsRegistry::observeNodeStoreTotals(db, busy.fn());

        // Ten labels now: the eight above plus both means.
        BEAST_EXPECT(busy.emitted.size() == 10);
        BEAST_EXPECT(busy.value("read_mean_us").has_value());
        BEAST_EXPECT(busy.value("write_mean_us").has_value());

        // Exact counts, so the denominators below are pinned independently.
        BEAST_EXPECT(busy.value("node_writes") == std::int64_t{kNumStored});
        BEAST_EXPECT(
            busy.value("node_reads_total") == std::int64_t{(2 * kNumStored) + kNumMissing});
        BEAST_EXPECT(busy.value("node_reads_hit") == std::int64_t{2 * kNumStored});
        // The two denominators differ, which is what makes the cross-checks
        // below able to catch a swapped accessor pair.
        BEAST_EXPECT(busy.value("node_reads_total") != busy.value("node_writes"));

        // Each mean is the truncating quotient of two OTHER published labels,
        // so this ties the three together without restating the helper's own
        // expression. read_mean_us fed the store pair fails it.
        auto const quotient = [](std::optional<std::int64_t> total,
                                 std::optional<std::int64_t> count) {
            return (total && count && *count != 0) ? std::optional<std::int64_t>{*total / *count}
                                                   : std::nullopt;
        };
        BEAST_EXPECT(
            busy.value("read_mean_us") ==
            quotient(busy.value("node_reads_duration_us"), busy.value("node_reads_total")));
        BEAST_EXPECT(
            busy.value("write_mean_us") ==
            quotient(busy.value("node_writes_duration_us"), busy.value("node_writes")));
    }

    /**
     * Verify observeWritePathDetail() publishes its four NuDB labels only for
     * a measuring backend, and omits the two derived means until an insert
     * has happened.
     */
    void
    testWritePathDetailLabels()
    {
        testcase("nodestore_state write-path labels");

        DummyScheduler scheduler;

        // Negative path first: a backend that does not measure its writes must
        // publish NOTHING. Zeros here would read as a perfectly idle write
        // path on a node whose write path is simply not instrumented.
        {
            beast::TempDir const memDb;
            Section memParams;
            memParams.set(Keys::kType, "memory");
            memParams.set(Keys::kPath, memDb.path());

            std::unique_ptr<Database> mem =
                Manager::instance().makeDatabase(megabytes(4), scheduler, 2, memParams, journal_);
            if (!BEAST_EXPECT(mem))
                return;

            auto const batch = createPredictableBatch(8, 111);
            storeBatch(*mem, batch);

            MetricSink sink;
            telemetry::MetricsRegistry::observeWritePathDetail(*mem, sink.fn());
            // Cause as well as state: the store really was written to, so the
            // emptiness is the std::nullopt branch and not an idle database.
            BEAST_EXPECT(sink.emitted.empty());
            BEAST_EXPECT(mem->getStoreCount() == 8);
        }

        beast::TempDir const nodeDb;
        Section nodeParams;
        nodeParams.set(Keys::kType, "nudb");
        nodeParams.set(Keys::kPath, nodeDb.path());

        std::unique_ptr<Database> db =
            Manager::instance().makeDatabase(megabytes(4), scheduler, 2, nodeParams, journal_);
        if (!BEAST_EXPECT(db))
            return;

        // Measuring backend, nothing written yet: the two instantaneous
        // values are published at zero while the two means are omitted. This
        // pins the deliberate asymmetry -- zero is meaningful for a gauge and
        // meaningless for a mean.
        MetricSink fresh;
        telemetry::MetricsRegistry::observeWritePathDetail(*db, fresh.fn());
        std::vector<std::string> const kFreshLabels{"nudb_insert_max_us", "nudb_writers_in_flight"};
        BEAST_EXPECT(fresh.names() == kFreshLabels);
        BEAST_EXPECT(fresh.value("nudb_writers_in_flight") == std::int64_t{0});
        BEAST_EXPECT(fresh.value("nudb_insert_max_us") == std::int64_t{0});
        BEAST_EXPECT(!fresh.value("nudb_insert_mean_us").has_value());
        BEAST_EXPECT(!fresh.value("nudb_writer_depth_x100").has_value());

        constexpr std::uint64_t kNumStored = 16;
        auto const stored = createPredictableBatch(static_cast<int>(kNumStored), 97531);
        storeBatch(*db, stored);

        MetricSink busy;
        telemetry::MetricsRegistry::observeWritePathDetail(*db, busy.fn());
        std::vector<std::string> const kBusyLabels{
            "nudb_insert_max_us",
            "nudb_insert_mean_us",
            "nudb_writer_depth_x100",
            "nudb_writers_in_flight"};
        BEAST_EXPECT(busy.names() == kBusyLabels);
        BEAST_EXPECT(busy.emitted.size() == kBusyLabels.size());

        // Writers all returned, so the live gauge is back to exactly zero
        // while the cumulative maximum stayed up.
        BEAST_EXPECT(busy.value("nudb_writers_in_flight") == std::int64_t{0});

        // One writing thread, so mean depth is exactly 1.00 and the x100
        // fixed-point form must read exactly 100. This is the assertion that
        // catches the scale being dropped (it would read 1) or applied twice
        // (10000) -- either of which makes the dashboard's divide-by-100 wrong
        // by two orders of magnitude.
        BEAST_EXPECT(busy.value("nudb_writer_depth_x100") == std::int64_t{100});
    }

    /**
     * Verify the nine acquire_* labels, published unconditionally, and each
     * wired to its own counter.
     *
     * Every expected value below is distinct, so a getter cross-wired to a
     * neighbouring counter cannot produce the right number anywhere.
     */
    void
    testAcquireStatLabels()
    {
        testcase("nodestore_state acquisition labels");

        std::vector<std::string> const kLabels{
            "acquire_aborts",
            "acquire_aborts_partial",
            "acquire_completions",
            "acquire_deferrals",
            "acquire_give_ups",
            "acquire_ledger_deferrals",
            "acquire_ledger_timeouts",
            "acquire_sweep_evictions",
            "acquire_timeouts"};

        // A quiet node publishes all nine at zero. Unlike a mean, zero is the
        // meaningful "no such event yet" reading for a counter, so omitting
        // these would lose the ability to see that nothing happened.
        AcquireStats const quiet;
        MetricSink fresh;
        telemetry::MetricsRegistry::observeAcquireStats(quiet, fresh.fn());
        BEAST_EXPECT(fresh.names() == kLabels);
        BEAST_EXPECT(fresh.emitted.size() == kLabels.size());
        for (auto const& label : kLabels)
            BEAST_EXPECT(fresh.value(label) == std::int64_t{0});

        // Distinct counts per event, and aborts recorded both with and
        // without partial work so the subset relationship is exercised: 5
        // aborts of which 2 discarded partly built maps.
        AcquireStats busy;
        for (int i = 0; i < 2; ++i)
            busy.recordDeferral(true);
        busy.recordDeferral(false);
        for (int i = 0; i < 5; ++i)
            busy.recordTimeout(true);
        for (int i = 0; i < 2; ++i)
            busy.recordTimeout(false);
        busy.recordGiveUp();
        for (int i = 0; i < 2; ++i)
            busy.recordAbort(true);
        for (int i = 0; i < 3; ++i)
            busy.recordAbort(false);
        for (int i = 0; i < 11; ++i)
            busy.recordCompletion();
        for (int i = 0; i < 13; ++i)
            busy.recordSweepEviction();

        MetricSink sink;
        telemetry::MetricsRegistry::observeAcquireStats(busy, sink.fn());
        BEAST_EXPECT(sink.names() == kLabels);
        BEAST_EXPECT(sink.value("acquire_deferrals") == std::int64_t{3});
        BEAST_EXPECT(sink.value("acquire_timeouts") == std::int64_t{7});
        BEAST_EXPECT(sink.value("acquire_give_ups") == std::int64_t{1});
        BEAST_EXPECT(sink.value("acquire_aborts") == std::int64_t{5});
        BEAST_EXPECT(sink.value("acquire_aborts_partial") == std::int64_t{2});
        BEAST_EXPECT(sink.value("acquire_completions") == std::int64_t{11});
        BEAST_EXPECT(sink.value("acquire_sweep_evictions") == std::int64_t{13});

        // The ledger-scoped pair must be a strict subset of the all-lane
        // totals, and the numbers are chosen to differ from them: a counter
        // wired to the wrong lane, or one that ignored the flag and counted
        // everything, would land on 3 and 7 instead of 2 and 5.
        BEAST_EXPECT(sink.value("acquire_ledger_deferrals") == std::int64_t{2});
        BEAST_EXPECT(sink.value("acquire_ledger_timeouts") == std::int64_t{5});
    }

    /**
     * Verify the four read-queue labels and that the two configured values
     * are forwarded rather than defaulted.
     */
    void
    testReadQueueLabels()
    {
        testcase("nodestore_state read-queue labels");

        DummyScheduler scheduler;
        beast::TempDir const nodeDb;
        // Three read threads and a bundle of 7, neither of which is the
        // default (the bundle default is 4), so a helper reading the wrong
        // JSON member cannot agree by coincidence.
        auto db = makeMeasuredDatabase(nodeDb, scheduler, 3);
        if (!BEAST_EXPECT(db))
            return;

        MetricSink sink;
        telemetry::MetricsRegistry::observeReadQueue(*db, sink.fn());

        std::vector<std::string> const kLabels{
            "read_queue", "read_request_bundle", "read_threads_running", "read_threads_total"};
        BEAST_EXPECT(sink.names() == kLabels);
        BEAST_EXPECT(sink.emitted.size() == kLabels.size());

        // Nothing has been queued for asynchronous read.
        BEAST_EXPECT(sink.value("read_queue") == std::int64_t{0});
        // Both configured values, exactly as requested. read_request_bundle is
        // the one that would silently read 4 if the helper looked up the wrong
        // JSON member, since 4 is the default.
        BEAST_EXPECT(sink.value("read_threads_total") == std::int64_t{3});
        BEAST_EXPECT(sink.value("read_request_bundle") == std::int64_t{7});
        // The running count races with the read threads parking themselves, so
        // only its bounds are assertable: present, non-negative, and never
        // above the total. Compared as unwrapped values, since comparing two
        // optionals would also pass with both absent.
        auto const running = sink.value("read_threads_running");
        BEAST_EXPECT(running.has_value());
        // Plain `if` rather than branching on BEAST_EXPECT's result: the
        // macro's return value hides the check from static analysis, which
        // then reads the dereferences below as unguarded.
        if (running.has_value())
        {
            BEAST_EXPECT(*running >= 0);
            BEAST_EXPECT(*running <= 3);
        }
    }

#endif  // XRPL_ENABLE_TELEMETRY

    void
    run() override
    {
        testCounterWidths();

        testDurationAccessors();

        testRotatingDurationAccessors();

#ifdef XRPL_ENABLE_TELEMETRY
        // The four gauge helpers are declared and defined only in a
        // telemetry-enabled build, so their label assertions are guarded the
        // same way.
        testNodeStoreTotalLabels();

        testWritePathDetailLabels();

        testAcquireStatLabels();

        testReadQueueLabels();
#endif

        testConfig();
    }
};

BEAST_DEFINE_TESTSUITE(DatabaseConfig, nodestore, xrpl);

}  // namespace xrpl::node_store
