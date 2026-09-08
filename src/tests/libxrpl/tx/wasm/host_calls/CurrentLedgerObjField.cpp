#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/WasmFixture.h>

#include <cstdint>
#include <expected>
#include <string>

namespace xrpl::test {

using testing::Return;

// home_le_field — a scalar field code in, bytes out.
struct CurrentLedgerObjFieldCall : HostCallTest
{
    // The field code the guest asks for. A real one, so the shim's `SField` lookup has
    // something to find.
    std::int32_t fieldCode = sfBalance.getCode();

    [[nodiscard]] std::string
    wat() const override
    {
        return std::string{R"wat(
(module
  (import "host_lib" "home_le_field" (func $home_le_field (param i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "escrow_finish") (result i32)
    (call $home_le_field (i32.const )wat"} +
            std::to_string(fieldCode) + R"wat() (i32.const 0) (i32.const 32))))
)wat";
    }
};

// The shim turns the guest's `i32` into the `SField` the C++ interface takes; asserting on
// the argument is what pins that translation rather than assuming it.
TEST_F(CurrentLedgerObjFieldCall, FieldCodeBecomesSFieldHostIsAskedFor)
{
    EXPECT_CALL(host, getCurrentLedgerObjField(testing::Ref(sfBalance)))
        .WillOnce(Return(Bytes{1, 2, 3}));

    EXPECT_EQ(hostAnswer(), 3) << "the length the host reported";
}

TEST_F(CurrentLedgerObjFieldCall, UnknownFieldCodeIsRefusedWithoutAskingHost)
{
    fieldCode = 0x7fff'0000;  // a type nothing is registered under
    EXPECT_CALL(host, getCurrentLedgerObjField).Times(0);

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::InvalidField));
}

TEST_F(CurrentLedgerObjFieldCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getCurrentLedgerObjField)
        .WillOnce(Return(std::unexpected(HostFunctionError::FieldNotFound)));

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::FieldNotFound));
}

// The field cap bounds the status, not just the bytes: a host reporting a length past
// `kMaxWasmDataLength` is too large whatever the guest's buffer was.
TEST_F(CurrentLedgerObjFieldCall, FieldPastProtocolCapIsTooLarge)
{
    EXPECT_CALL(host, getCurrentLedgerObjField)
        .WillOnce(Return(Bytes(kMaxWasmDataLength + 1, 0xab)));

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::DataFieldTooLarge));
}

}  // namespace xrpl::test
