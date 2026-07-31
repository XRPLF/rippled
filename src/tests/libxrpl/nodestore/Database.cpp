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
#include <memory>
#include <stdexcept>
#include <string>
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
