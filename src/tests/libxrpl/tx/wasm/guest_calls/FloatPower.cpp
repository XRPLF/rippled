#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostCallFixture.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>
#include <string_view>

namespace xrpl::test {

using testing::Return;

// float_pow — a float region, an exponent and a rounding mode in, a float region out.
// `FloatAdd.cpp` is where the reasoning behind these axes is written down; the difference
// here is that the second operand is a scalar, and it is followed by two more `i32`s that a
// permuted forward could put in its place.
struct FloatPowerGuest : HostCallTest
{
    static constexpr std::int32_t kXAt = 0;
    static constexpr std::int32_t kOutAt = 16;
    static constexpr std::int32_t kFloatLen = 12;
    static constexpr std::int32_t kN = 4;
    static constexpr std::int32_t kMode = 22;
    static constexpr std::string_view kXText = "float-pow-x0";

    static constexpr Arg kX = Arg::region(kXAt, kFloatLen);
    static constexpr Arg kDegree = Arg::scalar(kN);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kFloatLen);
    static constexpr Arg kRounding = Arg::scalar(kMode);

    Bytes const xBytes{kXText.begin(), kXText.end()};

    Bytes const result = [] {
        Bytes bytes(kFloatLen, 0xab);
        bytes[0] = 0x0d;
        bytes[1] = 0x0c;
        bytes[2] = 0x0b;
        bytes[3] = 0x0a;
        return bytes;
    }();

    [[nodiscard]] std::string
    watFor(Arg xArg, Arg nArg, Arg outArg, Arg modeArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "float_pow", {xArg, nArg, outArg, modeArg}, {{.at = kXAt, .bytes = xBytes}}, answer);
    }
};

TEST_F(FloatPowerGuest, OperandDegreeAndModeReachHostInOrder)
{
    EXPECT_CALL(host, floatPower(BytesAre(kXText), kN, kMode)).WillOnce(Return(result));

    auto const wat = watFor(kX, kDegree, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the float's first four bytes, little-endian";
}

TEST_F(FloatPowerGuest, StatusIsTheFloatsLength)
{
    EXPECT_CALL(host, floatPower(BytesAre(kXText), kN, kMode)).WillOnce(Return(result));

    auto const wat = watFor(kX, kDegree, kOut, kRounding, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kFloatLen);
}

TEST_F(FloatPowerGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatPower(BytesAre(kXText), kN, kMode))
        .WillOnce(Return(std::unexpected(HostFunctionError::FloatComputationError)));

    auto const wat = watFor(kX, kDegree, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::FloatComputationError));
}

TEST_F(FloatPowerGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, floatPower(BytesAre(kXText), kN, kMode))
        .WillOnce(testing::Throw(std::runtime_error{"float power came apart"}));

    auto const outcome = callHost(watFor(kX, kDegree, kOut, kRounding));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("floatPower"));
}

TEST_F(FloatPowerGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatPower).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kFloatLen), kDegree, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatPowerGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatPower).Times(0);

    auto const wat = watFor(Arg::region(-1, kFloatLen), kDegree, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(FloatPowerGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, floatPower(BytesAre(kXText), kN, kMode)).WillOnce(Return(result));

    auto const wat = watFor(kX, kDegree, Arg::outRegion(kOutAt, kFloatLen - 1), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(FloatPowerGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, floatPower(BytesAre(kXText), kN, kMode)).WillOnce(Return(result));

    auto const wat = watFor(kX, kDegree, Arg::outRegion(kOnePage, kFloatLen), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatPowerGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, floatPower(BytesAre(kXText), kN, kMode)).WillOnce(Return(result));

    auto const wat = watFor(kX, kDegree, Arg::outRegion(-1, kFloatLen), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
