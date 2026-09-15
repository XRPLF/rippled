#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/ContractCallFixture.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>

#include <expected>
#include <string>

namespace xrpl::test {

using testing::Return;

// function_param — the same shape as `instance_param`, over the call's own parameters.
// Which of the two tables the host is asked about is the whole of the difference.
struct FunctionParamCall : ContractCallTest
{
    [[nodiscard]] std::string
    wat() const override
    {
        return std::string{R"wat(
(module
  (import "host_lib" "function_param"
    (func $function_param (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)

  ;; Asks for parameter 2 as an STI_UINT32 (2) and returns the four bytes that landed.
  (func (export "escrow_finish") (result i32)
    (local $n i32)
    (local.set $n
      (call $function_param (i32.const 2) (i32.const 2) (i32.const 0) (i32.const 32)))
    (select (local.get $n) (i32.load (i32.const 0)) (i32.lt_s (local.get $n) (i32.const 0))))

  ;; Reports the length, for the cases where the value itself is not the point.
  (func (export "value_length") (result i32)
    (call $function_param (i32.const 2) (i32.const 2) (i32.const 0) (i32.const 32)))

  ;; A negative index, which the shim refuses rather than casting.
  (func (export "negative_index") (result i32)
    (call $function_param (i32.const -1) (i32.const 2) (i32.const 0) (i32.const 32))))
)wat"};
    }
};

TEST_F(FunctionParamCall, TheIndexAndTypeReachTheHostAndTheBytesComeBack)
{
    EXPECT_CALL(host, functionParam(2U, STI_UINT32))
        .WillOnce(Return(Bytes{0x0d, 0x0c, 0x0b, 0x0a}));
    EXPECT_CALL(host, instanceParam).Times(0);

    EXPECT_EQ(hostAnswer(), 0x0a0b0c0d) << "the value's bytes, read back little-endian";
}

TEST_F(FunctionParamCall, TheAnswerIsTheValuesLength)
{
    EXPECT_CALL(host, functionParam).WillOnce(Return(Bytes{1, 2, 3, 4}));

    EXPECT_EQ(hostAnswer("value_length"), 4);
}

// A negative index is out of range, not a very large one: casting it would ask the host
// about a parameter no contract could have meant.
TEST_F(FunctionParamCall, ANegativeIndexIsRefusedWithoutAskingTheHost)
{
    EXPECT_CALL(host, functionParam).Times(0);

    EXPECT_EQ(hostAnswer("negative_index"), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(FunctionParamCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, functionParam)
        .WillOnce(Return(std::unexpected(HostFunctionError::IndexOutOfBounds)));

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::IndexOutOfBounds));
}

}  // namespace xrpl::test
