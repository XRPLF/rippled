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

// float_from_mant_exp — an `i64` mantissa, an `i32` exponent and a rounding mode in, a float
// region out. The three scalars carry pairwise distinct values, so a permuted forward fails.
struct FloatFromMantExpGuest : GuestCallTest
{
    static constexpr std::int32_t kOutAt = 0;
    static constexpr std::int32_t kFloatLen = 12;
    static constexpr std::int64_t kMantissa = 0x0123'4567'89ab'cdefLL;
    static constexpr std::int32_t kExponent = -5;
    static constexpr std::int32_t kMode = 3;

    static constexpr Arg kMant = Arg::scalar64(kMantissa);
    static constexpr Arg kExp = Arg::scalar(kExponent);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kFloatLen);
    static constexpr Arg kRounding = Arg::scalar(kMode);

    Bytes const result = [] {
        Bytes bytes(kFloatLen, 0xab);
        bytes[0] = 0x0d;
        bytes[1] = 0x0c;
        bytes[2] = 0x0b;
        bytes[3] = 0x0a;
        return bytes;
    }();

    [[nodiscard]] static std::string
    watFor(
        Arg mantissaArg,
        Arg exponentArg,
        Arg outArg,
        Arg modeArg,
        Answer answer = Answer::WrittenBytes)
    {
        return hostCallWat(
            "float_from_mant_exp", {mantissaArg, exponentArg, outArg, modeArg}, {}, answer);
    }
};

TEST_F(FloatFromMantExpGuest, MantissaExponentAndModeReachHostInOrderAndFloatComesBack)
{
    EXPECT_CALL(host, floatFromMantExp(kMantissa, kExponent, kMode)).WillOnce(Return(result));

    auto const wat = watFor(kMant, kExp, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the float's first four bytes, little-endian";
}

TEST_F(FloatFromMantExpGuest, StatusIsTheFloatsLength)
{
    EXPECT_CALL(host, floatFromMantExp(kMantissa, kExponent, kMode)).WillOnce(Return(result));

    auto const wat = watFor(kMant, kExp, kOut, kRounding, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kFloatLen);
}

TEST_F(FloatFromMantExpGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatFromMantExp(kMantissa, kExponent, kMode))
        .WillOnce(Return(std::unexpected(HostFunctionError::FloatComputationError)));

    auto const wat = watFor(kMant, kExp, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::FloatComputationError));
}

TEST_F(FloatFromMantExpGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, floatFromMantExp(kMantissa, kExponent, kMode))
        .WillOnce(testing::Throw(std::runtime_error{"float from mant exp came apart"}));

    auto const outcome = run(watFor(kMant, kExp, kOut, kRounding));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("floatFromMantExp"));
}

TEST_F(FloatFromMantExpGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, floatFromMantExp(kMantissa, kExponent, kMode)).WillOnce(Return(result));

    auto const wat = watFor(kMant, kExp, Arg::outRegion(kOutAt, kFloatLen - 1), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(FloatFromMantExpGuest, OutRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatFromMantExp).Times(0);

    auto const wat = watFor(kMant, kExp, Arg::outRegion(kOnePage, kFloatLen), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatFromMantExpGuest, NegativeOutPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatFromMantExp).Times(0);

    auto const wat = watFor(kMant, kExp, Arg::outRegion(-1, kFloatLen), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
