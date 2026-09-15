#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
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

// emit_txn — a serialized transaction in, a TER out through the guest's buffer.
//
// The shim deserializes, so the host is handed a transaction rather than bytes: a guest that
// writes something that is not one never reaches it.
struct EmitTxnCall : ContractCallTest
{
    // A transaction as a contract would have serialized it.
    static STTx
    transaction()
    {
        return STTx{ttPAYMENT, [](STObject& obj) {
                        obj.setAccountID(sfAccount, account());
                        obj.setAccountID(sfDestination, account());
                        obj.setFieldAmount(sfAmount, STAmount{192});
                        obj.setFieldAmount(sfFee, STAmount{0});
                        obj.setFieldU32(sfSequence, 1);
                        obj.setFieldU32(sfFlags, tfInnerBatchTxn);
                        obj.setFieldVL(sfSigningPubKey, Blob{});
                    }};
    }

    static Bytes
    serialized()
    {
        auto const s = transaction().getSerializer();
        return Bytes{s.peekData().begin(), s.peekData().end()};
    }

    [[nodiscard]] std::string
    wat() const override
    {
        auto const bytes = serialized();
        return std::format(
            R"wat(
(module
  (import "host_lib" "emit_txn" (func $emit_txn (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "{}")
  (data (i32.const 2048) "not a transaction")

  ;; Emits the serialized transaction and returns the TER it wrote.
  (func (export "escrow_finish") (result i32)
    (local $n i32)
    (local.set $n
      (call $emit_txn (i32.const 0) (i32.const {}) (i32.const 3072) (i32.const 4)))
    (select (local.get $n) (i32.load (i32.const 3072)) (i32.lt_s (local.get $n) (i32.const 0))))

  (func (export "not_a_transaction") (result i32)
    (call $emit_txn (i32.const 2048) (i32.const 17) (i32.const 3072) (i32.const 4))))
)wat",
            escaped(bytes),
            bytes.size());
    }
};

TEST_F(EmitTxnCall, TheGuestsBytesReachTheHostAsATransaction)
{
    EXPECT_CALL(host, emitTxn(StTxIdIs(transaction().getTransactionID())))
        .WillOnce(Return(TERtoInt(tesSUCCESS)));

    EXPECT_EQ(hostAnswer(), TERtoInt(tesSUCCESS));
}

// The TER travels in the buffer because it may be negative, and a negative return is a host
// error. This is the case that would otherwise have stopped the run.
TEST_F(EmitTxnCall, ANegativeTerArrivesIntact)
{
    EXPECT_CALL(host, emitTxn).WillOnce(Return(TERtoInt(tefPAST_SEQ)));

    EXPECT_EQ(hostAnswer(), TERtoInt(tefPAST_SEQ));
}

TEST_F(EmitTxnCall, BytesThatAreNotATransactionAreRefusedWithoutAskingTheHost)
{
    EXPECT_CALL(host, emitTxn).Times(0);

    EXPECT_EQ(hostAnswer("not_a_transaction"), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(EmitTxnCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, emitTxn)
        .WillOnce(Return(std::unexpected(HostFunctionError::SubmitTxnFailure)));

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::SubmitTxnFailure));
}

}  // namespace xrpl::test
