#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>
#include <tx/wasm/fixtures/WasmFixture.h>

#include <expected>
#include <string>

namespace xrpl::test {

using testing::Return;

// emit_built_txn — a scalar in, and a TER out through the guest's buffer.
//
// The TER travels in a region rather than as the call's result because half the TER codes
// are negative, and a negative result is a `HostFunctionError`. These tests are what pins
// that: a `tec` and a `tem` must both arrive intact.
struct EmitBuiltTxnCall : HostCallTest
{
    [[nodiscard]] std::string
    wat() const override
    {
        return std::string{R"wat(
(module
  (import "host_lib" "emit_built_txn" (func $emit_built_txn (param i32 i32 i32) (result i32)))
  (memory (export "memory") 1)

  ;; Emits the transaction built at index 0 and returns the TER it wrote, so the test reads
  ;; the bytes that landed in memory rather than the call's length.
  (func (export "escrow_finish") (result i32)
    (local $n i32)
    (local.set $n (call $emit_built_txn (i32.const 0) (i32.const 0) (i32.const 4)))
    (select (local.get $n) (i32.load (i32.const 0)) (i32.lt_s (local.get $n) (i32.const 0))))

  ;; Reports the call's own answer: the number of bytes the TER took.
  (func (export "ter_length") (result i32)
    (call $emit_built_txn (i32.const 0) (i32.const 0) (i32.const 4)))

  ;; Offers three bytes, which cannot hold a TER.
  (func (export "short_buffer") (result i32)
    (call $emit_built_txn (i32.const 0) (i32.const 0) (i32.const 3))))
)wat"};
    }
};

TEST_F(EmitBuiltTxnCall, ASuccessfulEmitWritesTesSuccess)
{
    EXPECT_CALL(host, emitBuiltTxn(0U)).WillOnce(Return(TERtoInt(tesSUCCESS)));

    EXPECT_EQ(hostAnswer(), TERtoInt(tesSUCCESS));
}

// The reason the TER is not the call's result: this code is negative, and a negative result
// would have been read as a host error and stopped the run.
TEST_F(EmitBuiltTxnCall, ANegativeTerArrivesIntact)
{
    EXPECT_CALL(host, emitBuiltTxn).WillOnce(Return(TERtoInt(temMALFORMED)));

    EXPECT_EQ(hostAnswer(), TERtoInt(temMALFORMED));
}

TEST_F(EmitBuiltTxnCall, ATerIsFourBytes)
{
    EXPECT_CALL(host, emitBuiltTxn).WillOnce(Return(TERtoInt(tecUNFUNDED_PAYMENT)));

    EXPECT_EQ(hostAnswer("ter_length"), 4);
}

TEST_F(EmitBuiltTxnCall, ABufferTooSmallForATerIsRefused)
{
    EXPECT_CALL(host, emitBuiltTxn).WillOnce(Return(TERtoInt(tesSUCCESS)));

    EXPECT_EQ(hostAnswer("short_buffer"), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(EmitBuiltTxnCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, emitBuiltTxn)
        .WillOnce(Return(std::unexpected(HostFunctionError::SubmitTxnFailure)));

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::SubmitTxnFailure));
}

}  // namespace xrpl::test
