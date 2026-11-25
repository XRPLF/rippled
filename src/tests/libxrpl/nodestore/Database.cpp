#include <xrpl/nodestore/Database.h>

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/FileUtilities.h>
#include <xrpl/basics/scope.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Types.h>
#include <xrpl/protocol/SystemParameters.h>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>
#include <nodestore/TestBase.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace xrpl::node_store {

namespace {

std::vector<std::string>
allBackends()
{
#if XRPL_ROCKSDB_AVAILABLE
    return {"memory", "nudb", "rocksdb"};
#else
    return {"memory", "nudb"};
#endif
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

        beast::XorShiftEngine rng(kSeedValue);
        batch_ = createPredictableBatch(kNumObjects, rng());
    }

    std::unique_ptr<Database>
    makeDatabase()
    {
        return Manager::instance().makeDatabase(megabytes(4), scheduler_, 2, nodeParams_, journal_);
    }

    DummyScheduler scheduler_;
    TempDir const nodeDb_;
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
        beast::XorShiftEngine rng(kSeedValue);
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
    TempDir const nodeDb;
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
            auto const db =
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

namespace {

/**
 * Poll until the condition holds or the deadline passes. The read thread and
 * stop() both run off the test thread.
 *
 * @param condition What to wait for.
 * @param deadline The longest to wait.
 * @return Whether the condition held before the deadline.
 */
[[nodiscard]] bool
waitFor(
    std::function<bool()> const& condition,
    std::chrono::steady_clock::duration deadline = std::chrono::seconds{5})
{
    auto const giveUp = std::chrono::steady_clock::now() + deadline;
    while (std::chrono::steady_clock::now() < giveUp)
    {
        if (condition())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return condition();
}

}  // namespace

// Every read posted to the database completes exactly once, including reads still queued when
// stop() closes the queue and reads posted after it.
TEST(NodeStoreDatabase, stop_completes_every_read_once)
{
    DummyScheduler scheduler;
    TempDir const nodeDb;
    Section nodeParams;
    nodeParams.set("type", "memory");
    nodeParams.set("path", nodeDb.path());
    beast::Journal const journal(TestSink::instance());

    // One read thread, so a callback that blocks parks the whole queue.
    auto db = Manager::instance().makeDatabase(megabytes(4), scheduler, 1, nodeParams, journal);

    struct Outcome
    {
        std::atomic<int> calls{0};
        std::atomic<bool> null{false};
    };
    Outcome queued;
    Outcome blocked;
    Outcome late;
    std::atomic<bool> entered{false};
    std::atomic<bool> released{false};

    // ASSERT_TRUE below returns early. Release the parked callback and join the read thread
    // before the locals the callbacks capture go out of scope.
    ScopeExit const finish{[&] {
        released = true;
        db->stop();
    }};

    // The first read parks the read thread inside its callback until the test releases it.
    db->asyncFetch(UInt256{1}, 1, [&](std::shared_ptr<NodeObject> const& object) {
        entered = true;
        while (!released)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        // The result is published before the count, so a reader that sees the count sees it.
        blocked.null = !object;
        ++blocked.calls;
    });
    ASSERT_TRUE(waitFor([&] { return entered.load(); }));

    // With the thread parked, this read stays in the queue.
    db->asyncFetch(UInt256{2}, 1, [&](std::shared_ptr<NodeObject> const& object) {
        queued.null = !object;
        ++queued.calls;
    });

    // stop() closes the queue and completes the queued read with no object, then waits for the
    // read thread, which is still parked. So it runs on its own thread.
    std::thread stopper{[&] { db->stop(); }};
    EXPECT_TRUE(waitFor([&] { return queued.calls == 1; }));
    EXPECT_TRUE(queued.null);

    // A read posted after the queue closed completes at once, on the caller's thread.
    db->asyncFetch(UInt256{3}, 1, [&](std::shared_ptr<NodeObject> const& object) {
        late.null = !object;
        ++late.calls;
    });
    EXPECT_EQ(late.calls, 1);
    EXPECT_TRUE(late.null);

    released = true;
    stopper.join();

    // The parked read completed on the read thread, and nothing ran twice.
    EXPECT_EQ(blocked.calls, 1);
    EXPECT_TRUE(blocked.null);
    EXPECT_EQ(queued.calls, 1);
    EXPECT_EQ(late.calls, 1);
}

class DatabaseImportTest : public ::testing::TestWithParam<std::string>
{
};

TEST_P(DatabaseImportTest, same_backend)
{
    auto const type = GetParam();

    DummyScheduler scheduler;
    beast::Journal const journal(TestSink::instance());

    TempDir const srcDir;
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

        TempDir const destDir;
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
