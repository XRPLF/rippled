#include <xrpl/protocol/STJson.h>
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

// emit_event — a name and a serialized object in, a scalar out. The second of the two wire
// formats a guest writes itself, and a different parser from the one a value goes through.
struct EmitEventCall : ContractCallTest
{
    static STJson
    event()
    {
        return STJson{STJson::Map{
            {"count",
             std::static_pointer_cast<STBase>(std::make_shared<STUInt32>(sfSequence, 32U))}}};
    }

    [[nodiscard]] std::string
    wat() const override
    {
        auto const blob = event().toBlob();
        return std::format(
            R"wat(
(module
  (import "host_lib" "emit_event" (func $emit_event (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "transferred")
  (data (i32.const 32) "{}")
  (data (i32.const 2048) "not an object")

  (func (export "escrow_finish") (result i32)
    (call $emit_event (i32.const 0) (i32.const 11) (i32.const 32) (i32.const {})))

  (func (export "not_an_object") (result i32)
    (call $emit_event (i32.const 0) (i32.const 11) (i32.const 2048) (i32.const 13)))

  (func (export "empty_data") (result i32)
    (call $emit_event (i32.const 0) (i32.const 11) (i32.const 32) (i32.const 0))))
)wat",
            escaped(Bytes{blob.begin(), blob.end()}),
            blob.size());
    }
};

TEST_F(EmitEventCall, TheNameAndTheDecodedObjectReachTheHost)
{
    EXPECT_CALL(host, emitEvent(BytesAre("transferred"), EventJsonEq(event()))).WillOnce(Return(0));

    EXPECT_EQ(hostAnswer(), 0);
}

TEST_F(EmitEventCall, BytesThatAreNotAnObjectAreRefusedWithoutAskingTheHost)
{
    EXPECT_CALL(host, emitEvent).Times(0);

    EXPECT_EQ(hostAnswer("not_an_object"), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(EmitEventCall, AnEmptyRegionIsNotAnObject)
{
    EXPECT_CALL(host, emitEvent).Times(0);

    EXPECT_EQ(hostAnswer("empty_data"), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(EmitEventCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, emitEvent).WillOnce(Return(std::unexpected(HostFunctionError::InvalidState)));

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::InvalidState));
}

}  // namespace xrpl::test
