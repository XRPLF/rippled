#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// `getCurrentLedgerObjArrayLen` answers its count directly rather than through an out region:
// no axis E, no `OutRegion`, and the happy path asserts the returned count.
struct CurrentLedgerObjArrayLenCall : HostContextTest
{
    std::int32_t fieldCode = sfBalance.getCode();
};

TEST_F(CurrentLedgerObjArrayLenCall, FieldCodeBecomesSFieldHostIsAskedFor)
{
    EXPECT_CALL(host, getCurrentLedgerObjArrayLen(testing::Ref(sfBalance)))
        .WillOnce(testing::Return(5));

    EXPECT_EQ(hostContext.getCurrentLedgerObjArrayLen(fieldCode), 5);
}

// `NoArray` is what a field that is not an array actually answers, so it stands in for axis B
// here rather than an arbitrary code.
TEST_F(CurrentLedgerObjArrayLenCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getCurrentLedgerObjArrayLen(testing::Ref(sfBalance)))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::NoArray)));

    EXPECT_EQ(
        hostContext.getCurrentLedgerObjArrayLen(fieldCode),
        hfErrorToInt(HostFunctionError::NoArray));
}

TEST_F(CurrentLedgerObjArrayLenCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, getCurrentLedgerObjArrayLen(testing::Ref(sfBalance)))
        .WillOnce(testing::Throw(std::runtime_error{"current ledger obj array len came apart"}));

    EXPECT_EQ(
        hostContext.getCurrentLedgerObjArrayLen(fieldCode),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("current ledger obj array len came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getCurrentLedgerObjArrayLen"));
}

TEST_F(CurrentLedgerObjArrayLenCall, UnknownFieldCodeIsRefusedWithoutAskingHost)
{
    fieldCode = 0x7fff'0000;  // a code nothing is registered under
    EXPECT_CALL(host, getCurrentLedgerObjArrayLen).Times(0);

    EXPECT_EQ(
        hostContext.getCurrentLedgerObjArrayLen(fieldCode),
        hfErrorToInt(HostFunctionError::InvalidField));
}

}  // namespace xrpl::test
