#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace xrpl::test {

using testing::Return;

// float_to_int — a float region and a rounding mode in, eight little-endian bytes out.
struct FloatToIntGuest : GuestToHostCallTest
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

TEST_F(FloatToIntGuest, operand_and_mode_reach_host_in_order_and_int_comes_back)
{
    EXPECT_CALL(host, floatToInt(BytesAre(kXText), kMode)).WillOnce(Return(kResult));

    auto const wat = watFor(kX, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the int's first four bytes, little-endian";
}

}  // namespace xrpl::test
