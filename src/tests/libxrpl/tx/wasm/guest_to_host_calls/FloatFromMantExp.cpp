#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Return;

// float_from_mant_exp — an `i64` mantissa, an `i32` exponent and a rounding mode in, a float
// region out. The three scalars carry pairwise distinct values, so a permuted forward fails.
struct FloatFromMantExpGuest : GuestToHostCallTest
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

TEST_F(FloatFromMantExpGuest, mantissa_exponent_and_mode_reach_host_in_order_and_float_comes_back)
{
    EXPECT_CALL(host, floatFromMantExp(kMantissa, kExponent, kMode)).WillOnce(Return(result));

    auto const wat = watFor(kMant, kExp, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the float's first four bytes, little-endian";
}

}  // namespace xrpl::test
