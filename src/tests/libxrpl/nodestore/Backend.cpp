#include <xrpl/nodestore/Backend.h>

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/temp_dir.h>
#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/Types.h>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>
#include <nodestore/TestBase.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace xrpl::NodeStore {

namespace {

constexpr std::uint64_t kSeedValue = 50;
constexpr int kNumObjects = 2000;

std::vector<std::string>
backendTypes()
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

class BackendTypeTest : public ::testing::TestWithParam<std::string>
{
};

TEST_P(BackendTypeTest, RoundTrip)
{
    auto const type = GetParam();

    DummyScheduler scheduler;
    beast::TempDir const tempDir;
    Section params;
    params.set("type", type);
    params.set("path", tempDir.path());

    beast::xor_shift_engine rng(kSeedValue);
    auto batch = createPredictableBatch(kNumObjects, rng());

    beast::Journal const journal(TestSink::instance());

    {
        SCOPED_TRACE("write then read in order");
        auto backend = Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
        backend->open();
        storeBatch(*backend, batch);

        auto const copy = fetchCopyOfBatch(*backend, batch);
        EXPECT_TRUE(areBatchesEqual(batch, copy));

        std::shuffle(batch.begin(), batch.end(), rng);
        auto const shuffledCopy = fetchCopyOfBatch(*backend, batch);
        EXPECT_TRUE(areBatchesEqual(batch, shuffledCopy));
    }

    {
        SCOPED_TRACE("re-open and verify persistence");
        auto backend = Manager::instance().makeBackend(params, megabytes(4), scheduler, journal);
        backend->open();

        auto copy = fetchCopyOfBatch(*backend, batch);
        std::ranges::sort(batch, LessThan{});
        std::ranges::sort(copy, LessThan{});
        EXPECT_TRUE(areBatchesEqual(batch, copy));
    }
}

INSTANTIATE_TEST_SUITE_P(
    BackendTypes,
    BackendTypeTest,
    ::testing::ValuesIn(backendTypes()),
    [](::testing::TestParamInfo<std::string> const& info) { return info.param; });

}  // namespace xrpl::NodeStore
