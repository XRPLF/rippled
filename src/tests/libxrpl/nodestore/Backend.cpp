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
    types.emplace_back("sqlite");
#endif
    return types;
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
        EXPECT_TRUE(areBatchesEqual(batch_, copy));
    }

    {
        SCOPED_TRACE("read in shuffled order");
        beast::xor_shift_engine rng(kSeedValue);
        std::shuffle(batch_.begin(), batch_.end(), rng);
        auto const copy = fetchCopyOfBatch(*backend, batch_);
        EXPECT_TRUE(areBatchesEqual(batch_, copy));
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
    EXPECT_TRUE(areBatchesEqual(batch_, copy));
}

INSTANTIATE_TEST_SUITE_P(
    BackendTypes,
    BackendTypeTest,
    ::testing::ValuesIn(backendTypes()),
    [](::testing::TestParamInfo<std::string> const& info) { return info.param; });

}  // namespace xrpl::NodeStore
