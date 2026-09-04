#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <vector>

namespace xrpl::test {

// The engine's own rules - buffer-fit, the field cap, guest memory - are tested on the Rust
// side, not here.
//
// No out region and no axis E: `getLedgerObjNestedArrayLen` answers the array's element
// count directly rather than through a written buffer.
struct LedgerObjNestedArrayLenCall : HostContextTest
{
    std::int32_t const cacheIdx = 7;
    std::vector<std::int32_t> const steps{5, -12, 130};
    Bytes const locatorBytes = bytesOfSteps(steps);
};

TEST_F(LedgerObjNestedArrayLenCall, LocatorBytesBecomeFieldLocatorHostReturnsCount)
{
    EXPECT_CALL(host, getLedgerObjNestedArrayLen(cacheIdx, LocatorEquals(steps)))
        .WillOnce(testing::Return(7));

    EXPECT_EQ(hostContext.getLedgerObjNestedArrayLen(cacheIdx, bytesOf(locatorBytes)), 7);
}

// `NoArray` - the field the locator resolves to is not an array - is the error this shape
// most plausibly returns, so it stands in for axis B.
TEST_F(LedgerObjNestedArrayLenCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getLedgerObjNestedArrayLen(cacheIdx, LocatorEquals(steps)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::NoArray)));

    EXPECT_EQ(
        hostContext.getLedgerObjNestedArrayLen(cacheIdx, bytesOf(locatorBytes)),
        hfErrorToInt(HostFunctionError::NoArray));
}

TEST_F(LedgerObjNestedArrayLenCall, EmptyLocatorIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getLedgerObjNestedArrayLen).Times(0);

    EXPECT_EQ(
        hostContext.getLedgerObjNestedArrayLen(cacheIdx, bytesOf(Bytes{})),
        hfErrorToInt(HostFunctionError::LocatorMalformed));
}

// Distinct from an empty locator: `invokeWithLocator` checks the two conditions separately.
TEST_F(LedgerObjNestedArrayLenCall, MisalignedLocatorLengthIsRefusedWithoutAskingHost)
{
    Bytes const oddLength{1, 2, 3};
    EXPECT_CALL(host, getLedgerObjNestedArrayLen).Times(0);

    EXPECT_EQ(
        hostContext.getLedgerObjNestedArrayLen(cacheIdx, bytesOf(oddLength)),
        hfErrorToInt(HostFunctionError::LocatorMalformed));
}

TEST_F(LedgerObjNestedArrayLenCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, getLedgerObjNestedArrayLen(cacheIdx, LocatorEquals(steps)))
        .WillOnce(testing::Throw(std::runtime_error{"ledger obj nested array len came apart"}));

    EXPECT_EQ(
        hostContext.getLedgerObjNestedArrayLen(cacheIdx, bytesOf(locatorBytes)),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("ledger obj nested array len came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getLedgerObjNestedArrayLen"));
}

// `cacheIdx` is signed the whole way to the host, so 0 and a negative slot both cross
// unchanged.
TEST_F(LedgerObjNestedArrayLenCall, ZeroCacheIdxArrivesAtHostUnchanged)
{
    EXPECT_CALL(host, getLedgerObjNestedArrayLen(0, LocatorEquals(steps)))
        .WillOnce(testing::Return(7));

    EXPECT_EQ(hostContext.getLedgerObjNestedArrayLen(0, bytesOf(locatorBytes)), 7);
}

TEST_F(LedgerObjNestedArrayLenCall, NegativeCacheIdxArrivesAtHostUnchanged)
{
    std::int32_t const negativeCacheIdx = -3;
    EXPECT_CALL(host, getLedgerObjNestedArrayLen(negativeCacheIdx, LocatorEquals(steps)))
        .WillOnce(testing::Return(7));

    EXPECT_EQ(hostContext.getLedgerObjNestedArrayLen(negativeCacheIdx, bytesOf(locatorBytes)), 7);
}

}  // namespace xrpl::test
