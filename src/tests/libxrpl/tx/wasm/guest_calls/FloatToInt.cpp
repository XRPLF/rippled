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

// float_to_int — a float region and a rounding mode in, eight little-endian bytes out.
struct FloatToIntGuest : HostCallTest
{
    static constexpr std::int32_t kXAt = 0;
    static constexpr std::int32_t kOutAt = 16;
    static constexpr std::int32_t kFloatLen = 12;
    static constexpr std::int32_t kIntLen = 8;
    static constexpr std::int32_t kMode = 3;
    static constexpr std::string_view kXText = "float-toint0";
    static constexpr std::int64_t kResult = 0x1122'3344'0a0b'0c0dLL;

    static constexpr Arg kX = Arg::region(kXAt, kFloatLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kIntLen);
    static constexpr Arg kRounding = Arg::scalar(kMode);

    Bytes const xBytes{kXText.begin(), kXText.end()};

    [[nodiscard]] std::string
    watFor(Arg xArg, Arg outArg, Arg modeArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "float_to_int", {xArg, outArg, modeArg}, {{.at = kXAt, .bytes = xBytes}}, answer);
    }
};

TEST_F(FloatToIntGuest, OperandAndModeReachHostInOrderAndIntComesBack)
{
    EXPECT_CALL(host, floatToInt(BytesAre(kXText), kMode)).WillOnce(Return(kResult));

    auto const wat = watFor(kX, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the int's first four bytes, little-endian";
}

TEST_F(FloatToIntGuest, StatusIsTheIntsWidth)
{
    EXPECT_CALL(host, floatToInt(BytesAre(kXText), kMode)).WillOnce(Return(kResult));

    auto const wat = watFor(kX, kOut, kRounding, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kIntLen);
}

TEST_F(FloatToIntGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatToInt(BytesAre(kXText), kMode))
        .WillOnce(Return(std::unexpected(HostFunctionError::FloatComputationError)));

    auto const wat = watFor(kX, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::FloatComputationError));
}

TEST_F(FloatToIntGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, floatToInt(BytesAre(kXText), kMode))
        .WillOnce(testing::Throw(std::runtime_error{"float to int came apart"}));

    auto const outcome = callHost(watFor(kX, kOut, kRounding));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("floatToInt"));
}

TEST_F(FloatToIntGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatToInt).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kFloatLen), kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatToIntGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatToInt).Times(0);

    auto const wat = watFor(Arg::region(-1, kFloatLen), kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(FloatToIntGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, floatToInt(BytesAre(kXText), kMode)).WillOnce(Return(kResult));

    auto const wat = watFor(kX, Arg::outRegion(kOutAt, kIntLen - 1), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(FloatToIntGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, floatToInt(BytesAre(kXText), kMode)).WillOnce(Return(kResult));

    auto const wat = watFor(kX, Arg::outRegion(kOnePage, kIntLen), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatToIntGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, floatToInt(BytesAre(kXText), kMode)).WillOnce(Return(kResult));

    auto const wat = watFor(kX, Arg::outRegion(-1, kIntLen), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
