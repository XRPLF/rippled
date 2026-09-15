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

// set_data_nested_array_element_field — every kind of argument the ABI has in one call: an
// account, two string keys, a scalar index between them, and a typed value.
struct SetDataNestedArrayElementFieldCall : ContractCallTest
{
    [[nodiscard]] std::string
    wat() const override
    {
        return std::format(
            R"wat(
(module
  (import "host_lib" "set_data_nested_array_element_field"
    (func $set_data_nested_array_element_field
      (param i32 i32 i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "{}")
  (data (i32.const 32) "items")
  (data (i32.const 48) "price")
  (data (i32.const 64) "\10\2a")

  (func (export "escrow_finish") (result i32)
    (call $set_data_nested_array_element_field
      (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 5) (i32.const 7)
      (i32.const 48) (i32.const 5) (i32.const 64) (i32.const 2))))
)wat",
            escapedAccount());
    }
};

TEST_F(SetDataNestedArrayElementFieldCall, EveryArgumentArrivesInItsOwnPlace)
{
    EXPECT_CALL(
        host,
        setDataNestedArrayElementField(
            AccountIs(account()),
            BytesAre("items"),
            7U,
            BytesAre("price"),
            JsonValueIs(STI_UINT8, Bytes{0x2a})))
        .WillOnce(Return(0));

    EXPECT_EQ(hostAnswer(), 0);
}

TEST_F(SetDataNestedArrayElementFieldCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, setDataNestedArrayElementField)
        .WillOnce(Return(std::unexpected(HostFunctionError::InvalidState)));

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::InvalidState));
}

}  // namespace xrpl::test
