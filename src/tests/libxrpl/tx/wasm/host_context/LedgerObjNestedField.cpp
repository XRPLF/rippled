#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <vector>

namespace xrpl::test {

// The engine's own rules - buffer-fit, the field cap, guest memory - are tested on the Rust
// side, not here.
struct LedgerObjNestedFieldCall : HostContextTest
{
    std::int32_t const cacheIdx = 7;
    std::vector<std::int32_t> const steps{5, -12, 130};
    Bytes const locatorBytes = bytesOfSteps(steps);
};

TEST_F(LedgerObjNestedFieldCall, locator_bytes_become_field_locator_host_is_asked_for)
{
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getLedgerObjNestedField(cacheIdx, LocatorEquals(steps)))
        .WillOnce(testing::Return(value));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getLedgerObjNestedField(cacheIdx, bytesOf(locatorBytes), out.slice()),
        static_cast<std::int32_t>(value.size()));
    EXPECT_TRUE(out.holds(bytesOf(value)));
}

TEST_F(LedgerObjNestedFieldCall, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, getLedgerObjNestedField(cacheIdx, LocatorEquals(steps)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::NotLeafField)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getLedgerObjNestedField(cacheIdx, bytesOf(locatorBytes), out.slice()),
        hfErrorToInt(HostFunctionError::NotLeafField));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(LedgerObjNestedFieldCall, empty_locator_is_refused_without_asking_host)
{
    EXPECT_CALL(host, getLedgerObjNestedField).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getLedgerObjNestedField(cacheIdx, bytesOf(Bytes{}), out.slice()),
        hfErrorToInt(HostFunctionError::LocatorMalformed));
}

// Distinct from an empty locator: `invokeWithLocator` checks the two conditions separately.
TEST_F(LedgerObjNestedFieldCall, misaligned_locator_length_is_refused_without_asking_host)
{
    Bytes const oddLength{1, 2, 3};
    EXPECT_CALL(host, getLedgerObjNestedField).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getLedgerObjNestedField(cacheIdx, bytesOf(oddLength), out.slice()),
        hfErrorToInt(HostFunctionError::LocatorMalformed));
}

TEST_F(LedgerObjNestedFieldCall, host_exception_becomes_internal_fatal_and_is_logged)
{
    EXPECT_CALL(host, getLedgerObjNestedField(cacheIdx, LocatorEquals(steps)))
        .WillOnce(testing::Throw(std::runtime_error{"ledger obj nested field came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getLedgerObjNestedField(cacheIdx, bytesOf(locatorBytes), out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("ledger obj nested field came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getLedgerObjNestedField"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(LedgerObjNestedFieldCall, short_out_region_writes_nothing_and_returns_true_length)
{
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getLedgerObjNestedField(cacheIdx, LocatorEquals(steps)))
        .WillOnce(testing::Return(value));

    OutRegion out{value.size() - 1};
    EXPECT_EQ(
        hostContext.getLedgerObjNestedField(cacheIdx, bytesOf(locatorBytes), out.slice()),
        static_cast<std::int32_t>(value.size()));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(LedgerObjNestedFieldCall, out_region_of_exact_size_is_written)
{
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getLedgerObjNestedField(cacheIdx, LocatorEquals(steps)))
        .WillOnce(testing::Return(value));

    OutRegion out{value.size()};
    EXPECT_EQ(
        hostContext.getLedgerObjNestedField(cacheIdx, bytesOf(locatorBytes), out.slice()),
        static_cast<std::int32_t>(value.size()));
    EXPECT_TRUE(out.holds(bytesOf(value)));
}

TEST_F(LedgerObjNestedFieldCall, empty_result_answers_zero_and_writes_nothing)
{
    EXPECT_CALL(host, getLedgerObjNestedField(cacheIdx, LocatorEquals(steps)))
        .WillOnce(testing::Return(Bytes{}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.getLedgerObjNestedField(cacheIdx, bytesOf(locatorBytes), out.slice()), 0);
    EXPECT_FALSE(out.wasWritten());
}

// `cacheIdx` is signed the whole way to the host, so 0 and a negative slot both cross
// unchanged.
TEST_F(LedgerObjNestedFieldCall, zero_cache_idx_arrives_at_host_unchanged)
{
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getLedgerObjNestedField(0, LocatorEquals(steps)))
        .WillOnce(testing::Return(value));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getLedgerObjNestedField(0, bytesOf(locatorBytes), out.slice()),
        static_cast<std::int32_t>(value.size()));
}

TEST_F(LedgerObjNestedFieldCall, negative_cache_idx_arrives_at_host_unchanged)
{
    std::int32_t const negativeCacheIdx = -3;
    Bytes const value{1, 2, 3};
    EXPECT_CALL(host, getLedgerObjNestedField(negativeCacheIdx, LocatorEquals(steps)))
        .WillOnce(testing::Return(value));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.getLedgerObjNestedField(negativeCacheIdx, bytesOf(locatorBytes), out.slice()),
        static_cast<std::int32_t>(value.size()));
}

}  // namespace xrpl::test
