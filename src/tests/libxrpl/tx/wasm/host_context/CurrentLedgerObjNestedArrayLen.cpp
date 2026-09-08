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
// No out region and no axis E: `getCurrentLedgerObjNestedArrayLen` answers the array's
// element count directly rather than through a written buffer.
struct CurrentLedgerObjNestedArrayLenCall : HostContextTest
{
    std::vector<std::int32_t> const steps{5, -12, 130};
    Bytes const locatorBytes = bytesOfSteps(steps);
};

TEST_F(CurrentLedgerObjNestedArrayLenCall, LocatorBytesBecomeFieldLocatorHostReturnsCount)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedArrayLen(LocatorEquals(steps)))
        .WillOnce(testing::Return(7));

    EXPECT_EQ(hostContext.getCurrentLedgerObjNestedArrayLen(bytesOf(locatorBytes)), 7);
}

// `NoArray` - the field the locator resolves to is not an array - is the error this shape
// most plausibly returns, so it stands in for axis B.
TEST_F(CurrentLedgerObjNestedArrayLenCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedArrayLen(LocatorEquals(steps)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::NoArray)));

    EXPECT_EQ(
        hostContext.getCurrentLedgerObjNestedArrayLen(bytesOf(locatorBytes)),
        hfErrorToInt(HostFunctionError::NoArray));
}

TEST_F(CurrentLedgerObjNestedArrayLenCall, EmptyLocatorIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedArrayLen).Times(0);

    EXPECT_EQ(
        hostContext.getCurrentLedgerObjNestedArrayLen(bytesOf(Bytes{})),
        hfErrorToInt(HostFunctionError::LocatorMalformed));
}

// Distinct from an empty locator: `invokeWithLocator` checks the two conditions separately.
TEST_F(CurrentLedgerObjNestedArrayLenCall, MisalignedLocatorLengthIsRefusedWithoutAskingHost)
{
    Bytes const oddLength{1, 2, 3};
    EXPECT_CALL(host, getCurrentLedgerObjNestedArrayLen).Times(0);

    EXPECT_EQ(
        hostContext.getCurrentLedgerObjNestedArrayLen(bytesOf(oddLength)),
        hfErrorToInt(HostFunctionError::LocatorMalformed));
}

TEST_F(CurrentLedgerObjNestedArrayLenCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, getCurrentLedgerObjNestedArrayLen(LocatorEquals(steps)))
        .WillOnce(
            testing::Throw(std::runtime_error{"current ledger obj nested array len came apart"}));

    EXPECT_EQ(
        hostContext.getCurrentLedgerObjNestedArrayLen(bytesOf(locatorBytes)),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("current ledger obj nested array len came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getCurrentLedgerObjNestedArrayLen"));
}

}  // namespace xrpl::test
