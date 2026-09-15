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

// get_data_nested_array_element_field — the widest shape in the ABI: two string keys with a
// scalar index between them, then the output region.
struct GetDataNestedArrayElementFieldCall : ContractCallTest
{
    [[nodiscard]] std::string
    wat() const override
    {
        return std::format(
            R"wat(
(module
  (import "host_lib" "get_data_nested_array_element_field"
    (func $get_data_nested_array_element_field
      (param i32 i32 i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "{}")
  (data (i32.const 32) "items")
  (data (i32.const 48) "price")

  (func (export "escrow_finish") (result i32)
    (local $n i32)
    (local.set $n (call $get_data_nested_array_element_field
      (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 5) (i32.const 7)
      (i32.const 48) (i32.const 5) (i32.const 64) (i32.const 32)))
    (select (local.get $n) (i32.load (i32.const 64)) (i32.lt_s (local.get $n) (i32.const 0))))

  (func (export "negative_index") (result i32)
    (call $get_data_nested_array_element_field
      (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 5) (i32.const -1)
      (i32.const 48) (i32.const 5) (i32.const 64) (i32.const 32))))
)wat",
            escapedAccount());
    }
};

TEST_F(GetDataNestedArrayElementFieldCall, EachRegionAndTheIndexArriveInOrder)
{
    EXPECT_CALL(
        host,
        getDataNestedArrayElementField(
            AccountIs(account()), BytesAre("items"), 7U, BytesAre("price")))
        .WillOnce(Return(Bytes{0x0d, 0x0c, 0x0b, 0x0a}));

    EXPECT_EQ(hostAnswer(), 0x0a0b0c0d);
}

TEST_F(GetDataNestedArrayElementFieldCall, ANegativeIndexIsRefusedWithoutAskingTheHost)
{
    EXPECT_CALL(host, getDataNestedArrayElementField).Times(0);

    EXPECT_EQ(hostAnswer("negative_index"), hfErrorToInt(HostFunctionError::IndexOutOfBounds));
}

TEST_F(GetDataNestedArrayElementFieldCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getDataNestedArrayElementField)
        .WillOnce(Return(std::unexpected(HostFunctionError::InvalidField)));

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::InvalidField));
}

}  // namespace xrpl::test
