#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/ContractCallFixture.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>

#include <expected>
#include <format>
#include <string>

namespace xrpl::test {

using testing::Return;

// set_data_nested_object_field — two string keys and a typed value.
//
// The value's type byte decides what the shim builds, and the outer key comes first, so
// both are asserted against something the other could not be.
struct SetDataNestedObjectFieldCall : ContractCallTest
{
    [[nodiscard]] std::string
    wat() const override
    {
        return std::format(
            R"wat(
(module
  (import "host_lib" "set_data_nested_object_field"
    (func $set_data_nested_object_field (param i32 i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "{}")
  (data (i32.const 32) "stats")
  (data (i32.const 48) "score")
  (data (i32.const 64) "\02\00\00\27\0f")

  (func (export "escrow_finish") (result i32)
    (call $set_data_nested_object_field
      (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 5) (i32.const 48) (i32.const 5)
      (i32.const 64) (i32.const 5))))
)wat",
            escapedAccount());
    }
};

TEST_F(SetDataNestedObjectFieldCall, TheOuterKeyComesFirstAndTheValueIsDecoded)
{
    EXPECT_CALL(
        host,
        setDataNestedObjectField(
            AccountIs(account()),
            BytesAre("stats"),
            BytesAre("score"),
            JsonValueIs(STI_UINT32, (Bytes{0x00, 0x00, 0x27, 0x0f}))))
        .WillOnce(Return(0));

    EXPECT_EQ(hostAnswer(), 0);
}

TEST_F(SetDataNestedObjectFieldCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, setDataNestedObjectField)
        .WillOnce(Return(std::unexpected(HostFunctionError::InvalidState)));

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::InvalidState));
}

}  // namespace xrpl::test
