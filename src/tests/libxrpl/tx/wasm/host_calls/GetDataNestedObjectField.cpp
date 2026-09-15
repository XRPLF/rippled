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

// get_data_nested_object_field — two string keys in, bytes out.
//
// The outer key comes first. Two regions of the same kind are the easiest pair to hand over
// backwards, and nothing downstream would notice, so the order is asserted with keys that
// could not be mistaken for one another.
struct GetDataNestedObjectFieldCall : ContractCallTest
{
    [[nodiscard]] std::string
    wat() const override
    {
        return std::format(
            R"wat(
(module
  (import "host_lib" "get_data_nested_object_field"
    (func $get_data_nested_object_field (param i32 i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "{}")
  (data (i32.const 32) "stats")
  (data (i32.const 48) "score")

  (func (export "escrow_finish") (result i32)
    (local $n i32)
    (local.set $n (call $get_data_nested_object_field
      (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 5) (i32.const 48) (i32.const 5)
      (i32.const 64) (i32.const 32)))
    (select (local.get $n) (i32.load (i32.const 64)) (i32.lt_s (local.get $n) (i32.const 0)))))
)wat",
            escapedAccount());
    }
};

TEST_F(GetDataNestedObjectFieldCall, TheOuterKeyIsTheFirstRegion)
{
    EXPECT_CALL(
        host, getDataNestedObjectField(AccountIs(account()), BytesAre("stats"), BytesAre("score")))
        .WillOnce(Return(Bytes{0x0d, 0x0c, 0x0b, 0x0a}));

    EXPECT_EQ(hostAnswer(), 0x0a0b0c0d);
}

TEST_F(GetDataNestedObjectFieldCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getDataNestedObjectField)
        .WillOnce(Return(std::unexpected(HostFunctionError::InvalidField)));

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::InvalidField));
}

}  // namespace xrpl::test
