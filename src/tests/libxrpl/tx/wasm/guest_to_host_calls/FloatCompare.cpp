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

// float_cmp — two operand regions in, and the verdict returned directly, so this is the one
// float call with no out region and no status to read.
//
// The verdict is a tri-state — 0 equal, 1 first greater, 2 second greater — which is why it
// never collides with a negative error code.
struct FloatCompareGuest : GuestToHostCallTest
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

TEST_F(FloatCompareGuest, operands_reach_host_in_order_and_verdict_is_the_answer)
{
    EXPECT_CALL(host, floatCompare(BytesAre(kXText), BytesAre(kYText)))
        .WillOnce(Return(FloatOrdering::Less));

    auto const wat = watFor(kX, kY);
    EXPECT_EQ(hostAnswer(wat), floatOrderingToInt(FloatOrdering::Less));
}

}  // namespace xrpl::test
