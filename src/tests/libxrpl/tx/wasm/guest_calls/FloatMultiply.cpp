#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestCallFixture.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>
#include <string_view>

namespace xrpl::test {

using testing::Return;

// float_mult — `float_add`'s shape, and `FloatAdd.cpp` is where the reasoning behind these
// axes is written down.
struct FloatMultiplyGuest : GuestCallTest
{
    static constexpr std::int32_t kXAt = 0;
    static constexpr std::int32_t kYAt = 16;
    static constexpr std::int32_t kOutAt = 32;
    static constexpr std::int32_t kFloatLen = 12;
    static constexpr std::int32_t kMode = 5;
    static constexpr std::string_view kXText = "float-mul-x0";
    static constexpr std::string_view kYText = "float-mul-y0";

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
            "float_mult",
            {xArg, yArg, outArg, modeArg},
            {{.at = kXAt, .bytes = xBytes}, {.at = kYAt, .bytes = yBytes}},
            answer);
    }
};

TEST_F(FloatMultiplyGuest, OperandsAndModeReachHostInOrder)
{
    EXPECT_CALL(host, floatMultiply(BytesAre(kXText), BytesAre(kYText), kMode))
        .WillOnce(Return(result));

    auto const wat = watFor(kX, kY, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the float's first four bytes, little-endian";
}

TEST_F(FloatMultiplyGuest, StatusIsTheFloatsLength)
{
    EXPECT_CALL(host, floatMultiply(BytesAre(kXText), BytesAre(kYText), kMode))
        .WillOnce(Return(result));

    auto const wat = watFor(kX, kY, kOut, kRounding, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kFloatLen);
}

TEST_F(FloatMultiplyGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatMultiply(BytesAre(kXText), BytesAre(kYText), kMode))
        .WillOnce(Return(std::unexpected(HostFunctionError::FloatComputationError)));

    auto const wat = watFor(kX, kY, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::FloatComputationError));
}

TEST_F(FloatMultiplyGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, floatMultiply(BytesAre(kXText), BytesAre(kYText), kMode))
        .WillOnce(testing::Throw(std::runtime_error{"float multiply came apart"}));

    auto const outcome = run(watFor(kX, kY, kOut, kRounding));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("floatMultiply"));
}

TEST_F(FloatMultiplyGuest, FirstOperandPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatMultiply).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kFloatLen), kY, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatMultiplyGuest, SecondOperandPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatMultiply).Times(0);

    auto const wat = watFor(kX, Arg::region(kOnePage, kFloatLen), kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatMultiplyGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatMultiply).Times(0);

    auto const wat = watFor(Arg::region(-1, kFloatLen), kY, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(FloatMultiplyGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, floatMultiply(BytesAre(kXText), BytesAre(kYText), kMode))
        .WillOnce(Return(result));

    auto const wat = watFor(kX, kY, Arg::outRegion(kOutAt, kFloatLen - 1), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(FloatMultiplyGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, floatMultiply(BytesAre(kXText), BytesAre(kYText), kMode))
        .WillOnce(Return(result));

    auto const wat = watFor(kX, kY, Arg::outRegion(kOnePage, kFloatLen), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatMultiplyGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, floatMultiply(BytesAre(kXText), BytesAre(kYText), kMode))
        .WillOnce(Return(result));

    auto const wat = watFor(kX, kY, Arg::outRegion(-1, kFloatLen), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
