#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>

namespace xrpl::test {

using testing::Return;

// float_from_int — an `i64` and a rounding mode in, a float region out.
//
// What only this layer can show is that `CxxHost`'s hand-written forward
// (`crates/xrpl-wasm-vm-ffi/src/lib.rs`) reaches the C++ host with the guest's arguments.
// Declaration order is wasm parameter order, so `mode` is the *last* parameter, after the
// out region's pointer and length — an engine reading it as a length would still typecheck.
struct FloatFromIntGuest : GuestCallTest
{
    static constexpr std::int32_t kOutAt = 0;
    static constexpr std::int32_t kFloatLen = 12;
    static constexpr std::int32_t kMode = 1;

    // Wider than 32 bits and not a palindrome, so a crossing that truncated or byte-swapped
    // it would reach the host as a different number. `float_from_int` and
    // `float_from_mant_exp` are the ABI's only genuine `i64` parameters.
    static constexpr std::int64_t kX = 0x0123'4567'89ab'cdefLL;

    static constexpr Arg kInt = Arg::scalar64(kX);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kFloatLen);
    static constexpr Arg kRounding = Arg::scalar(kMode);

    // A float whose first four bytes are distinctive, so the guest's `i32.load` of them
    // cannot pass by accident.
    Bytes const result = [] {
        Bytes bytes(kFloatLen, 0xab);
        bytes[0] = 0x0d;
        bytes[1] = 0x0c;
        bytes[2] = 0x0b;
        bytes[3] = 0x0a;
        return bytes;
    }();

    [[nodiscard]] static std::string
    watFor(Arg intArg, Arg outArg, Arg modeArg, Answer answer = Answer::WrittenBytes)
    {
        return hostCallWat("float_from_int", {intArg, outArg, modeArg}, {}, answer);
    }
};

TEST_F(FloatFromIntGuest, IntAndModeReachHostInOrderAndFloatComesBack)
{
    EXPECT_CALL(host, floatFromInt(kX, kMode)).WillOnce(Return(result));

    auto const wat = watFor(kInt, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the float's first four bytes, little-endian";
}

TEST_F(FloatFromIntGuest, StatusIsTheFloatsLength)
{
    EXPECT_CALL(host, floatFromInt(kX, kMode)).WillOnce(Return(result));

    auto const wat = watFor(kInt, kOut, kRounding, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kFloatLen);
}

// Nothing between the guest and the host judges the mode, so a value naming no rounding rule
// is the host's to refuse rather than the engine's.
TEST_F(FloatFromIntGuest, ModeOutOfRangeCrossesVerbatim)
{
    static constexpr std::int32_t kNonsenseMode = 424242;
    EXPECT_CALL(host, floatFromInt(kX, kNonsenseMode))
        .WillOnce(Return(std::unexpected(HostFunctionError::FloatComputationError)));

    auto const wat = watFor(kInt, kOut, Arg::scalar(kNonsenseMode));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::FloatComputationError));
}

TEST_F(FloatFromIntGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatFromInt(kX, kMode))
        .WillOnce(Return(std::unexpected(HostFunctionError::FloatComputationError)));

    auto const wat = watFor(kInt, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::FloatComputationError));
}

TEST_F(FloatFromIntGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, floatFromInt(kX, kMode))
        .WillOnce(testing::Throw(std::runtime_error{"float from int came apart"}));

    auto const outcome = run(watFor(kInt, kOut, kRounding));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("floatFromInt"));
}

// This call reads no guest memory, so `abi.rs`'s `write_into` hands the host the guest's own
// region and judges where it points *before* calling: only the fit, which is judged against
// the length the host reports, is decided after.
TEST_F(FloatFromIntGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, floatFromInt(kX, kMode)).WillOnce(Return(result));

    auto const wat = watFor(kInt, Arg::outRegion(kOutAt, kFloatLen - 1), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(FloatFromIntGuest, OutRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatFromInt).Times(0);

    auto const wat = watFor(kInt, Arg::outRegion(kOnePage, kFloatLen), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatFromIntGuest, NegativeOutPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatFromInt).Times(0);

    auto const wat = watFor(kInt, Arg::outRegion(-1, kFloatLen), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
