#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Return;

// base_fee — no input, one scalar written out.
struct BaseFeeGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kOutAt = 0;
    static constexpr std::int32_t kFeeLen = 4;

    static constexpr Arg kOut = Arg::outRegion(kOutAt, kFeeLen);

    static constexpr std::uint32_t kBaseFeeDrops = 0x2a3b4c5d;

    [[nodiscard]] static std::string
    watFor(Arg outArg, Answer answer = Answer::WrittenBytes)
    {
        return hostCallWat("base_fee", {outArg}, {}, answer);
    }
};

TEST_F(BaseFeeGuest, base_fee_reaches_guest_as_four_little_endian_bytes)
{
    EXPECT_CALL(host, getBaseFee()).WillOnce(Return(kBaseFeeDrops));

    auto const wat = watFor(kOut);
    EXPECT_EQ(hostAnswer(wat), static_cast<std::int32_t>(kBaseFeeDrops));
}

}  // namespace xrpl::test
