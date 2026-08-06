#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/WasmFixture.h>

#include <expected>
#include <string>
#include <string_view>

namespace xrpl::test {

using testing::Return;

// trace — two byte inputs and a flag, no output.
struct TraceCall : HostCallTest
{
    [[nodiscard]] std::string
    wat() const override
    {
        return std::string{R"wat(
(module
  (import "host_lib" "trace" (func $trace (param i32 i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "note")
  (data (i32.const 16) "\07\08")

  (func (export "escrow_finish") (result i32)
    (call $trace (i32.const 0) (i32.const 4) (i32.const 16) (i32.const 2) (i32.const 1)))

  (func (export "not_as_hex") (result i32)
    (call $trace (i32.const 0) (i32.const 4) (i32.const 16) (i32.const 2) (i32.const 0))))
)wat"};
    }
};

// Two borrowed regions in one call, which is the shape a single-input helper could not
// express — so this pins that both arrive intact, and the flag with them.
TEST_F(TraceCall, MessageDataAndFlagAllArrive)
{
    EXPECT_CALL(host, trace(std::string_view("note"), BytesAre("\x07\x08"), true))
        .WillOnce(Return(0));

    EXPECT_EQ(hostAnswer(), 0) << "a call with nothing to report answers 0";
}

TEST_F(TraceCall, HexFlagIsGuestsToChoose)
{
    EXPECT_CALL(host, trace(testing::_, testing::_, false)).WillOnce(Return(0));

    EXPECT_EQ(hostAnswer("not_as_hex"), 0);
}

TEST_F(TraceCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, trace).WillOnce(Return(std::unexpected(HostFunctionError::InvalidParams)));

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
