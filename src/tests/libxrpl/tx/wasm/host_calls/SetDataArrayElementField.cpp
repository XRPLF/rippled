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

// set_data_array_element_field — a key, a scalar index between the regions, and a value.
struct SetDataArrayElementFieldCall : ContractCallTest
{
    [[nodiscard]] std::string
    wat() const override
    {
        return std::format(
            R"wat(
(module
  (import "host_lib" "set_data_array_element_field"
    (func $set_data_array_element_field (param i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "{}")
  (data (i32.const 32) "amount")
  (data (i32.const 64) "\10\2a")

  (func (export "escrow_finish") (result i32)
    (call $set_data_array_element_field
      (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 6) (i32.const 7)
      (i32.const 64) (i32.const 2)))

  (func (export "negative_index") (result i32)
    (call $set_data_array_element_field
      (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 6) (i32.const -1)
      (i32.const 64) (i32.const 2)))

  (func (export "value_with_extra_byte") (result i32)
    (call $set_data_array_element_field
      (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 6) (i32.const 7)
      (i32.const 64) (i32.const 3))))
)wat",
            escapedAccount());
    }
};

TEST_F(SetDataArrayElementFieldCall, the_key_the_index_and_the_value_all_arrive)
{
    EXPECT_CALL(
        host,
        setDataArrayElementField(
            AccountIs(account()), 7U, BytesAre("amount"), JsonValueIs(STI_UINT8, Bytes{0x2a})))
        .WillOnce(Return(0));

    EXPECT_EQ(hostAnswer(), 0);
}

TEST_F(SetDataArrayElementFieldCall, a_negative_index_is_refused_without_asking_the_host)
{
    EXPECT_CALL(host, setDataArrayElementField).Times(0);

    EXPECT_EQ(hostAnswer("negative_index"), hfErrorToInt(HostFunctionError::IndexOutOfBounds));
}

// A value is one type code and that type's bytes; a byte past them is not part of it.
TEST_F(SetDataArrayElementFieldCall, a_value_with_bytes_left_over_is_refused)
{
    EXPECT_CALL(host, setDataArrayElementField).Times(0);

    EXPECT_EQ(hostAnswer("value_with_extra_byte"), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(SetDataArrayElementFieldCall, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, setDataArrayElementField)
        .WillOnce(Return(std::unexpected(HostFunctionError::InvalidState)));

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::InvalidState));
}

}  // namespace xrpl::test
