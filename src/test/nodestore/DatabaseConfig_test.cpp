#include <test/jtx/CheckMessageLogs.h>
#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpld/core/Config.h>

#include <xrpl/basics/Blob.h>
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
#include <xrpl/nodestore/Types.h>
#include <xrpl/nodestore/detail/DatabaseRotatingImp.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/rdb/DatabaseCon.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

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

        // A 64-bit counter must be able to represent the byte totals a
        // long-lived node reaches. 32 bits cannot.
        static_assert(
            std::numeric_limits<std::uint64_t>::max() > std::numeric_limits<std::uint32_t>::max(),
            "64-bit counters must exceed the 32-bit ceiling");

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

        DummyScheduler scheduler;

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
        storeBatch(*db, stored);

        BEAST_EXPECT(db->getStoreCount() == kNumStored);
        BEAST_EXPECT(db->getStoreDurationUs() > 0);

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
        BEAST_EXPECT(db->getFetchDurationUs() > 0);

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

        DummyScheduler scheduler;

        beast::TempDir const writableDir;
        beast::TempDir const archiveDir;

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
        if (!BEAST_EXPECT(writableBackend) || !BEAST_EXPECT(archiveBackend))
            return;
        writableBackend->open();
        archiveBackend->open();

        DatabaseRotatingImp rotating(
            scheduler,
            2,
            std::move(writableBackend),
            std::move(archiveBackend),
            writableParams,
            journal_);

        // The private fetchNodeObject override hides the public base overload,
        // so exercise the rotating store through the Database interface, which
        // is also how production callers reach it.
        Database& db = rotating;

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
        BEAST_EXPECT(db.getFetchDurationUs() > 0);
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

    void
    run() override
    {
        testCounterWidths();

        testDurationAccessors();

        testRotatingDurationAccessors();

        testConfig();
    }
};

BEAST_DEFINE_TESTSUITE(DatabaseConfig, nodestore, xrpl);

}  // namespace xrpl::node_store
