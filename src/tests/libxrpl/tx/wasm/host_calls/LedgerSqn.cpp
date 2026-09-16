#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostCallFixture.h>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace xrpl::test {

using testing::Return;

// ldgr_index — no input, one scalar output.
struct LedgerSqnCall : HostCallTest
{
    static constexpr std::int32_t kOutAt = 0;
    static constexpr std::int32_t kSeqLen = 4;

    static constexpr Arg kOut = Arg::outRegion(kOutAt, kSeqLen);

    [[nodiscard]] static std::string
    watFor(Arg outArg, Answer answer = Answer::WrittenBytes)
    {
        return hostCallWat("ldgr_index", {outArg}, {}, answer);
    }
};

TEST_F(LedgerSqnCall, SequenceReachesGuestAsFourLittleEndianBytes)
{
    EXPECT_CALL(host, getLedgerSqn()).WillOnce(Return(0x01020304u));

    // Read back with `i32.load`, which is little-endian by the wasm spec — so the value
    // arriving intact is the byte order being right.
    auto const wat = watFor(kOut);
    EXPECT_EQ(hostAnswer(wat), 0x01020304);
}

TEST_F(LedgerSqnCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, getLedgerSqn())
        .WillOnce(Return(std::unexpected(HostFunctionError::LedgerObjNotFound)));

    auto const wat = watFor(kOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::LedgerObjNotFound));
}

// The engine decides the fit, not the host: the host is never told the guest's capacity, it
// reports the value's true length and the engine turns a length past the buffer into
// `BufferTooSmall` — with nothing written.
//
// Its own module, because showing that the refusal wrote *nothing* needs the guest to read
// its memory back after the call, which is more than one host call's worth of module.
TEST_F(LedgerSqnCall, BufferTooSmallIsRefusedWholeNotTruncated)
{
    EXPECT_CALL(host, getLedgerSqn()).WillOnce(Return(0x01020304u));

    // Two bytes is not enough for the value. Returns the host's code while memory is still
    // zero, or 1 if anything was written into it — so a refused write is visibly a refusal
    // and not a truncation.
    static constexpr std::string_view kWat = R"wat(
(module
  (import "host_lib" "ldgr_index" (func $ldgr_index (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "escrow_finish") (result i32)
    (local $n i32)
    (local.set $n (call $ldgr_index (i32.const 0) (i32.const 2)))
    (select (local.get $n) (i32.const 1) (i32.eqz (i32.load (i32.const 0))))))
)wat";

    EXPECT_EQ(hostAnswer(kWat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

}  // namespace xrpl::test
