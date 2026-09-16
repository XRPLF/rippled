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

// float_sub — `float_add`'s shape, and `FloatAdd.cpp` is where the reasoning behind these
// axes is written down. Subtraction does not commute, so the operands' order is the one thing
// here a caller could get wrong and still compute something.
struct FloatSubtractGuest : HostCallTest
{
    static constexpr std::int32_t kXAt = 0;
    static constexpr std::int32_t kYAt = 16;
    static constexpr std::int32_t kOutAt = 32;
    static constexpr std::int32_t kFloatLen = 12;
    static constexpr std::int32_t kMode = 13;
    static constexpr std::string_view kXText = "float-sub-x0";
    static constexpr std::string_view kYText = "float-sub-y0";

    static constexpr Arg kX = Arg::region(kXAt, kFloatLen);
    static constexpr Arg kY = Arg::region(kYAt, kFloatLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kFloatLen);
    static constexpr Arg kRounding = Arg::scalar(kMode);

    Bytes const xBytes{kXText.begin(), kXText.end()};
    Bytes const yBytes{kYText.begin(), kYText.end()};

    Bytes const result = [] {
        Bytes bytes(kFloatLen, 0xab);
        bytes[0] = 0x0d;
        bytes[1] = 0x0c;
        bytes[2] = 0x0b;
        bytes[3] = 0x0a;
        return bytes;
    }();

    [[nodiscard]] std::string
    watFor(Arg xArg, Arg yArg, Arg outArg, Arg modeArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "float_sub",
            {xArg, yArg, outArg, modeArg},
            {{.at = kXAt, .bytes = xBytes}, {.at = kYAt, .bytes = yBytes}},
            answer);
    }
};

TEST_F(FloatSubtractGuest, OperandsAndModeReachHostInOrder)
{
    EXPECT_CALL(host, floatSubtract(BytesAre(kXText), BytesAre(kYText), kMode))
        .WillOnce(Return(result));

    auto const wat = watFor(kX, kY, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the float's first four bytes, little-endian";
}

TEST_F(FloatSubtractGuest, StatusIsTheFloatsLength)
{
    EXPECT_CALL(host, floatSubtract(BytesAre(kXText), BytesAre(kYText), kMode))
        .WillOnce(Return(result));

    auto const wat = watFor(kX, kY, kOut, kRounding, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kFloatLen);
}

TEST_F(FloatSubtractGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatSubtract(BytesAre(kXText), BytesAre(kYText), kMode))
        .WillOnce(Return(std::unexpected(HostFunctionError::FloatComputationError)));

    auto const wat = watFor(kX, kY, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::FloatComputationError));
}

TEST_F(FloatSubtractGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, floatSubtract(BytesAre(kXText), BytesAre(kYText), kMode))
        .WillOnce(testing::Throw(std::runtime_error{"float subtract came apart"}));

    auto const outcome = callHost(watFor(kX, kY, kOut, kRounding));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("floatSubtract"));
}

TEST_F(FloatSubtractGuest, FirstOperandPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatSubtract).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kFloatLen), kY, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatSubtractGuest, SecondOperandPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatSubtract).Times(0);

    auto const wat = watFor(kX, Arg::region(kOnePage, kFloatLen), kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatSubtractGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatSubtract).Times(0);

    auto const wat = watFor(Arg::region(-1, kFloatLen), kY, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(FloatSubtractGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, floatSubtract(BytesAre(kXText), BytesAre(kYText), kMode))
        .WillOnce(Return(result));

    auto const wat = watFor(kX, kY, Arg::outRegion(kOutAt, kFloatLen - 1), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(FloatSubtractGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, floatSubtract(BytesAre(kXText), BytesAre(kYText), kMode))
        .WillOnce(Return(result));

    auto const wat = watFor(kX, kY, Arg::outRegion(kOnePage, kFloatLen), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatSubtractGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, floatSubtract(BytesAre(kXText), BytesAre(kYText), kMode))
        .WillOnce(Return(result));

    auto const wat = watFor(kX, kY, Arg::outRegion(-1, kFloatLen), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
