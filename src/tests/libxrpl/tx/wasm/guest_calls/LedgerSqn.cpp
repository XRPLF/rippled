#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestCallFixture.h>

#include <cstdint>
#include <expected>
#include <format>
#include <string>
#include <string_view>

namespace xrpl::test {

using testing::Return;

// ldgr_index — no input, one scalar output.
struct LedgerSqnGuest : GuestCallTest
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

TEST_F(LedgerSqnGuest, SequenceReachesGuestAsFourLittleEndianBytes)
{
    EXPECT_CALL(host, getLedgerSqn()).WillOnce(Return(0x01020304u));

    // Read back with `i32.load`, which is little-endian by the wasm spec — so the value
    // arriving intact is the byte order being right.
    auto const wat = watFor(kOut);
    EXPECT_EQ(hostAnswer(wat), 0x01020304);
}

TEST_F(LedgerSqnGuest, HostErrorBecomesContractReturnValue)
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
TEST_F(LedgerSqnGuest, BufferTooSmallIsRefusedWholeNotTruncated)
{
    EXPECT_CALL(host, getLedgerSqn()).WillOnce(Return(0x01020304u));

    // Two bytes is not enough for the value. Returns the host's code while memory is still
    // zero, or 1 if anything was written into it — so a refused write is visibly a refusal
    // and not a truncation.
    auto const wat = std::format(
        R"wat(
(module
  (import "host_lib" "ldgr_index" (func $ldgr_index (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "{}") (result i32)
    (local $n i32)
    (local.set $n (call $ldgr_index (i32.const 0) (i32.const 2)))
    (select (local.get $n) (i32.const 1) (i32.eqz (i32.load (i32.const 0))))))
)wat",
        escrowFunctionName);

    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(LedgerSqnGuest, StatusIsTheScalarWidth)
{
    EXPECT_CALL(host, getLedgerSqn()).WillOnce(Return(0x01020304u));

    auto const wat = watFor(kOut, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kSeqLen);
}

TEST_F(LedgerSqnGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, getLedgerSqn())
        .WillOnce(testing::Throw(std::runtime_error{"ledger sequence came apart"}));

    auto const outcome = run(watFor(kOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("getLedgerSqn"));
}

TEST_F(LedgerSqnGuest, OutRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getLedgerSqn).Times(0);

    auto const wat = watFor(Arg::outRegion(kOnePage, kSeqLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(LedgerSqnGuest, NegativeOutPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, getLedgerSqn).Times(0);

    auto const wat = watFor(Arg::outRegion(-1, kSeqLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}
}  // namespace xrpl::test
