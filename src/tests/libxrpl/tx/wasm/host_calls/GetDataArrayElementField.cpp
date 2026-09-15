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

// get_data_array_element_field — the shape with a scalar *between* two regions: account,
// key, index, out.
//
// The key comes before the index on the wire, and the host takes them the other way round.
// A test that only checked both arrived would pass with them swapped, so this asserts each
// against a value the other could not be.
struct GetDataArrayElementFieldCall : ContractCallTest
{
    [[nodiscard]] std::string
    wat() const override
    {
        return std::format(
            R"wat(
(module
  (import "host_lib" "get_data_array_element_field"
    (func $get_data_array_element_field (param i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "{}")
  (data (i32.const 32) "amount")

  (func (export "escrow_finish") (result i32)
    (local $n i32)
    (local.set $n (call $get_data_array_element_field
      (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 6) (i32.const 7)
      (i32.const 64) (i32.const 32)))
    (select (local.get $n) (i32.load (i32.const 64)) (i32.lt_s (local.get $n) (i32.const 0))))

  ;; A negative element index, which the shim refuses rather than casting to a huge one.
  (func (export "negative_index") (result i32)
    (call $get_data_array_element_field
      (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 6) (i32.const -1)
      (i32.const 64) (i32.const 32))))
)wat",
            escapedAccount());
    }
};

TEST_F(GetDataArrayElementFieldCall, TheKeyIsTheRegionAndTheIndexIsTheScalar)
{
    EXPECT_CALL(host, getDataArrayElementField(AccountIs(account()), 7U, BytesAre("amount")))
        .WillOnce(Return(Bytes{0x0d, 0x0c, 0x0b, 0x0a}));

    EXPECT_EQ(hostAnswer(), 0x0a0b0c0d);
}

TEST_F(GetDataArrayElementFieldCall, ANegativeIndexIsRefusedWithoutAskingTheHost)
{
    EXPECT_CALL(host, getDataArrayElementField).Times(0);

    EXPECT_EQ(hostAnswer("negative_index"), hfErrorToInt(HostFunctionError::IndexOutOfBounds));
}

TEST_F(GetDataArrayElementFieldCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getDataArrayElementField)
        .WillOnce(Return(std::unexpected(HostFunctionError::InvalidState)));

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::InvalidState));
}

}  // namespace xrpl::test
