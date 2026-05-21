#include <xrpl/nodestore/Database.h>

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/temp_dir.h>
#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/Types.h>
#include <xrpl/protocol/SystemParameters.h>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>
#include <nodestore/TestBase.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace xrpl::NodeStore {

namespace {

constexpr std::int64_t kSeedValue = 50;
constexpr int kNumObjects = 2000;

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
    types.push_back("sqlite");
#endif
    return types;
}

}  // namespace

class NodeStoreDatabaseTest : public ::testing::TestWithParam<std::string>
{
};

class NodeStoreDatabasePersistenceTest : public ::testing::TestWithParam<std::string>
{
};

TEST_P(NodeStoreDatabaseTest, StoreAndFetch)
{
    auto const type = GetParam();

    DummyScheduler scheduler;
    beast::TempDir const nodeDb;
    Section nodeParams;
    nodeParams.set("type", type);
    nodeParams.set("path", nodeDb.path());

    beast::Journal const journal(TestSink::instance());
    beast::xor_shift_engine rng(kSeedValue);
    auto batch = createPredictableBatch(kNumObjects, rng());

    auto db = Manager::instance().makeDatabase(megabytes(4), scheduler, 2, nodeParams, journal);

    storeBatch(*db, batch);

    {
        SCOPED_TRACE("read in original order");
        auto const copy = fetchCopyOfBatch(*db, batch);
        EXPECT_TRUE(areBatchesEqual(batch, copy));
    }

    {
        SCOPED_TRACE("read in shuffled order");
        std::shuffle(batch.begin(), batch.end(), rng);
        auto const copy = fetchCopyOfBatch(*db, batch);
        EXPECT_TRUE(areBatchesEqual(batch, copy));
    }
}

TEST_P(NodeStoreDatabasePersistenceTest, RoundTrip)
{
    auto const type = GetParam();

    DummyScheduler scheduler;
    beast::TempDir const nodeDb;
    Section nodeParams;
    nodeParams.set("type", type);
    nodeParams.set("path", nodeDb.path());

    beast::Journal const journal(TestSink::instance());
    beast::xor_shift_engine rng(kSeedValue);
    auto batch = createPredictableBatch(kNumObjects, rng());

    {
        auto db = Manager::instance().makeDatabase(megabytes(4), scheduler, 2, nodeParams, journal);
        storeBatch(*db, batch);
    }

    // re-open without the ephemeral db
    auto db = Manager::instance().makeDatabase(megabytes(4), scheduler, 2, nodeParams, journal);

    auto copy = fetchCopyOfBatch(*db, batch);
    std::ranges::sort(batch, LessThan{});
    std::ranges::sort(copy, LessThan{});
    EXPECT_TRUE(areBatchesEqual(batch, copy));
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

TEST(NodeStoreDatabase, MemoryEarliestSeq)
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

TEST_P(DatabaseImportTest, SameBackend)
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
    EXPECT_TRUE(areBatchesEqual(batch, copy));
}

INSTANTIATE_TEST_SUITE_P(
    ImportBackends,
    DatabaseImportTest,
    ::testing::ValuesIn(importBackends()),
    [](::testing::TestParamInfo<std::string> const& info) { return info.param; });

}  // namespace xrpl::NodeStore
