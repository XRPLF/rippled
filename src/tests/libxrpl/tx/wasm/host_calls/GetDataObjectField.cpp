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

// get_data_object_field — an account region and a string key in, bytes out.
//
// The key is declared `&str`, so the engine checks it is UTF-8 before the host sees it: a
// region that is not becomes `InvalidParams` without a call.
struct GetDataObjectFieldCall : ContractCallTest
{
    [[nodiscard]] std::string
    wat() const override
    {
        return std::format(
            R"wat(
(module
  (import "host_lib" "get_data_object_field"
    (func $get_data_object_field (param i32 i32 i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "{}")
  (data (i32.const 32) "count")
  (data (i32.const 48) "\ff\fe")

  (func (export "escrow_finish") (result i32)
    (local $n i32)
    (local.set $n (call $get_data_object_field
      (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 5) (i32.const 64) (i32.const 32)))
    (select (local.get $n) (i32.load (i32.const 64)) (i32.lt_s (local.get $n) (i32.const 0))))

  ;; A nineteen-byte account, which is not an account id.
  (func (export "short_account") (result i32)
    (call $get_data_object_field
      (i32.const 0) (i32.const 19) (i32.const 32) (i32.const 5) (i32.const 64) (i32.const 32)))

  ;; A key region that is not UTF-8.
  (func (export "invalid_key") (result i32)
    (call $get_data_object_field
      (i32.const 0) (i32.const 20) (i32.const 48) (i32.const 2) (i32.const 64) (i32.const 32))))
)wat",
            escapedAccount());
    }
};

TEST_F(GetDataObjectFieldCall, TheAccountAndKeyReachTheHostAndTheValueComesBack)
{
    EXPECT_CALL(host, getDataObjectField(AccountIs(account()), BytesAre("count")))
        .WillOnce(Return(Bytes{0x0d, 0x0c, 0x0b, 0x0a}));

    EXPECT_EQ(hostAnswer(), 0x0a0b0c0d);
}

TEST_F(GetDataObjectFieldCall, AnAccountRegionOfTheWrongLengthIsRefusedWithoutAskingTheHost)
{
    EXPECT_CALL(host, getDataObjectField).Times(0);

    EXPECT_EQ(hostAnswer("short_account"), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(GetDataObjectFieldCall, AKeyThatIsNotTextIsRefusedWithoutAskingTheHost)
{
    EXPECT_CALL(host, getDataObjectField).Times(0);

    EXPECT_EQ(hostAnswer("invalid_key"), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(GetDataObjectFieldCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getDataObjectField)
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

}  // namespace xrpl::test
