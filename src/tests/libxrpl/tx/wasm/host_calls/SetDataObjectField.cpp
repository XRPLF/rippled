#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>
#include <tx/wasm/fixtures/WasmFixture.h>

#include <cstdint>
#include <expected>
#include <format>
#include <string>

namespace xrpl::test {

using testing::Return;

// set_data_object_field — an account region, a string key, and a typed value the host has
// to decode: the shape no escrow host function has.
struct SetDataObjectFieldCall : HostCallTest
{
    // A 20-byte account the guest holds as bytes, distinctive enough that a wrong region
    // cannot match it.
    static AccountID
    account()
    {
        AccountID id;
        id.begin()[0] = 0xae;
        id.begin()[19] = 0xea;
        return id;
    }

    // What the guest writes for `42` as an `STI_UINT8`: the type byte, then the value.
    std::string valueBytes{"\\10\\2a"};
    std::uint32_t valueLength{2};

    [[nodiscard]] std::string
    wat() const override
    {
        auto const id = account();
        std::string escaped;
        for (auto const b : id)
            escaped += std::format("\\{:02x}", b);

        return std::format(
            R"wat(
(module
  (import "host_lib" "set_data_object_field"
    (func $set_data_object_field (param i32 i32 i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "{}")
  (data (i32.const 32) "value_u8")
  (data (i32.const 64) "{}")

  (func (export "escrow_finish") (result i32)
    (call $set_data_object_field
      (i32.const 0) (i32.const 20)
      (i32.const 32) (i32.const 8)
      (i32.const 64) (i32.const {}))))
)wat",
            escaped,
            valueBytes,
            valueLength);
    }
};

// The whole wire contract in one call: the account arrives as an id, the key as a view over
// the guest's bytes, and the value as the `STJson::Value` its type byte names.
TEST_F(SetDataObjectFieldCall, AccountKeyAndDecodedValueReachHost)
{
    EXPECT_CALL(
        host,
        setDataObjectField(
            AccountIs(account()), BytesAre("value_u8"), JsonValueIs(STI_UINT8, Bytes{0x2a})))
        .WillOnce(Return(0));

    EXPECT_EQ(hostAnswer(), 0);
}

TEST_F(SetDataObjectFieldCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, setDataObjectField)
        .WillOnce(Return(std::unexpected(HostFunctionError::InvalidState)));

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::InvalidState));
}

// The value is decoded before the host is reached, so bytes that hold no field are the
// shim's refusal rather than something the host has to recognize.
TEST_F(SetDataObjectFieldCall, AnEmptyValueIsRefusedWithoutAskingTheHost)
{
    valueBytes.clear();
    valueLength = 0;
    EXPECT_CALL(host, setDataObjectField).Times(0);

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
