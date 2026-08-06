#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/WasmFixture.h>

#include <cstdint>
#include <expected>
#include <limits>
#include <string>
#include <string_view>

namespace xrpl::test {

using testing::Return;

// trace_num — a string and an i64, the ABI's only 64-bit parameter.
struct TraceNumCall : HostCallTest
{
    [[nodiscard]] std::string
    wat() const override
    {
        return std::string{R"wat(
(module
  (import "host_lib" "trace_num" (func $trace_num (param i32 i32 i64) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "count")

  (func (export "escrow_finish") (result i32)
    (call $trace_num (i32.const 0) (i32.const 5) (i64.const -9223372036854775808))))
)wat"};
    }
};

// The extreme value on purpose: an `i64` that a truncating or sign-losing conversion anywhere
// on the wire would visibly mangle.
TEST_F(TraceNumCall, I64ArrivesWholeIncludingMostNegativeValue)
{
    EXPECT_CALL(host, traceNum(std::string_view("count"), std::numeric_limits<std::int64_t>::min()))
        .WillOnce(Return(0));

    EXPECT_EQ(hostAnswer(), 0);
}

TEST_F(TraceNumCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, traceNum)
        .WillOnce(Return(std::unexpected(HostFunctionError::IndexOutOfBounds)));

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::IndexOutOfBounds));
}

}  // namespace xrpl::test
