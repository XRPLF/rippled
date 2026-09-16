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

// float_cmp — two operand regions in, and the verdict returned directly, so this is the one
// float call with no out region and no status to read.
//
// The verdict is a tri-state — 0 equal, 1 first greater, 2 second greater — which is why it
// never collides with a negative error code.
struct FloatCompareGuest : GuestCallTest
{
    static constexpr std::int32_t kXAt = 0;
    static constexpr std::int32_t kYAt = 16;
    static constexpr std::int32_t kFloatLen = 12;
    static constexpr std::string_view kXText = "float-cmp-x0";
    static constexpr std::string_view kYText = "float-cmp-y0";

    static constexpr Arg kX = Arg::region(kXAt, kFloatLen);
    static constexpr Arg kY = Arg::region(kYAt, kFloatLen);

    Bytes const xBytes{kXText.begin(), kXText.end()};
    Bytes const yBytes{kYText.begin(), kYText.end()};

    [[nodiscard]] std::string
    watFor(Arg xArg, Arg yArg) const
    {
        return hostCallWat(
            "float_cmp",
            {xArg, yArg},
            {{.at = kXAt, .bytes = xBytes}, {.at = kYAt, .bytes = yBytes}});
    }
};

TEST_F(FloatCompareGuest, OperandsReachHostInOrderAndVerdictIsTheAnswer)
{
    EXPECT_CALL(host, floatCompare(BytesAre(kXText), BytesAre(kYText))).WillOnce(Return(2));

    auto const wat = watFor(kX, kY);
    EXPECT_EQ(hostAnswer(wat), 2);
}

TEST_F(FloatCompareGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatCompare(BytesAre(kXText), BytesAre(kYText)))
        .WillOnce(Return(std::unexpected(HostFunctionError::FloatInputMalformed)));

    auto const wat = watFor(kX, kY);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::FloatInputMalformed));
}

TEST_F(FloatCompareGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, floatCompare(BytesAre(kXText), BytesAre(kYText)))
        .WillOnce(testing::Throw(std::runtime_error{"float compare came apart"}));

    auto const outcome = run(watFor(kX, kY));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("floatCompare"));
}

TEST_F(FloatCompareGuest, FirstOperandPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatCompare).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kFloatLen), kY);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatCompareGuest, SecondOperandPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatCompare).Times(0);

    auto const wat = watFor(kX, Arg::region(kOnePage, kFloatLen));
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatCompareGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatCompare).Times(0);

    auto const wat = watFor(Arg::region(-1, kFloatLen), kY);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
