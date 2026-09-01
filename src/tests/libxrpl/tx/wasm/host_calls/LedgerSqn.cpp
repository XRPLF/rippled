#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/WasmFixture.h>

#include <expected>
#include <string>

namespace xrpl::test {

using testing::Return;

// ldgr_index — no input, one scalar output.
struct LedgerSqnCall : HostCallTest
{
    [[nodiscard]] std::string
    wat() const override
    {
        return std::string{R"wat(
(module
  (import "host_lib" "ldgr_index" (func $ldgr_index (param i32 i32) (result i32)))
  (memory (export "memory") 1)

  ;; Four bytes is what the value needs. Returns what the host wrote, or its error code.
  (func (export "escrow_finish") (result i32)
    (local $n i32)
    (local.set $n (call $ldgr_index (i32.const 0) (i32.const 4)))
    (select (local.get $n) (i32.load (i32.const 0)) (i32.lt_s (local.get $n) (i32.const 0))))

  ;; Two bytes is not enough for the value. Returns the host's code when memory is still
  ;; zero, or 1 if anything was written into it - so a refused write is visibly a refusal
  ;; and not a truncation.
  (func (export "into_two_bytes") (result i32)
    (local $n i32)
    (local.set $n (call $ldgr_index (i32.const 0) (i32.const 2)))
    (select (local.get $n) (i32.const 1) (i32.eqz (i32.load (i32.const 0))))))
)wat"};
    }
};

TEST_F(LedgerSqnCall, SequenceReachesGuestAsFourLittleEndianBytes)
{
    EXPECT_CALL(host, getLedgerSqn()).WillOnce(Return(0x01020304u));

    // Read back with `i32.load`, which is little-endian by the wasm spec — so the value
    // arriving intact is the byte order being right.
    EXPECT_EQ(hostAnswer(), 0x01020304);
}

TEST_F(LedgerSqnCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getLedgerSqn())
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    EXPECT_EQ(hostAnswer(), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

// The engine decides the fit, not the host: the host is never told the guest's capacity, it
// reports the value's true length and the engine turns a length past the buffer into
// `BufferTooSmall` — with nothing written.
TEST_F(LedgerSqnCall, BufferTooSmallIsRefusedWholeNotTruncated)
{
    EXPECT_CALL(host, getLedgerSqn()).WillOnce(Return(0x01020304u));

    EXPECT_EQ(hostAnswer("into_two_bytes"), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

}  // namespace xrpl::test
