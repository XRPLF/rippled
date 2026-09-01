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
//
// No out region and no axis E: `getTxNestedArrayLen` answers the array's element count
// directly rather than through a written buffer.
struct TxNestedArrayLenCall : HostContextTest
{
    std::vector<std::int32_t> const steps{5, -12, 130};
    Bytes const locatorBytes = bytesOfSteps(steps);
};

TEST_F(TxNestedArrayLenCall, LocatorBytesBecomeFieldLocatorHostReturnsCount)
{
    EXPECT_CALL(host, getTxNestedArrayLen(LocatorEquals(steps))).WillOnce(testing::Return(7));

    EXPECT_EQ(hostContext.getTxNestedArrayLen(bytesOf(locatorBytes)), 7);
}

// `NoArray` - the field the locator resolves to is not an array - is the error this shape
// most plausibly returns, so it stands in for axis B.
TEST_F(TxNestedArrayLenCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getTxNestedArrayLen(LocatorEquals(steps)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::NoArray)));

    EXPECT_EQ(
        hostContext.getTxNestedArrayLen(bytesOf(locatorBytes)),
        hfErrorToInt(HostFunctionError::NoArray));
}

TEST_F(TxNestedArrayLenCall, EmptyLocatorIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getTxNestedArrayLen).Times(0);

    EXPECT_EQ(
        hostContext.getTxNestedArrayLen(bytesOf(Bytes{})),
        hfErrorToInt(HostFunctionError::LocatorMalformed));
}

// Distinct from an empty locator: `invokeWithLocator` checks the two conditions separately.
TEST_F(TxNestedArrayLenCall, MisalignedLocatorLengthIsRefusedWithoutAskingHost)
{
    Bytes const oddLength{1, 2, 3};
    EXPECT_CALL(host, getTxNestedArrayLen).Times(0);

    EXPECT_EQ(
        hostContext.getTxNestedArrayLen(bytesOf(oddLength)),
        hfErrorToInt(HostFunctionError::LocatorMalformed));
}

TEST_F(TxNestedArrayLenCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, getTxNestedArrayLen(LocatorEquals(steps)))
        .WillOnce(testing::Throw(std::runtime_error{"tx nested array len came apart"}));

    EXPECT_EQ(
        hostContext.getTxNestedArrayLen(bytesOf(locatorBytes)),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("tx nested array len came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getTxNestedArrayLen"));
}

}  // namespace xrpl::test
