#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Eq;
using testing::Return;

namespace {

Bytes
serialized(STAmount const& amount)
{
    Serializer s;
    amount.add(s);
    return s.getData();
}

}  // namespace

// float_from_stamount — a serialized `STAmount` region and a rounding mode in, a float region
// out.
struct FloatFromSTAmountGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kAmountAt = 0;
    static constexpr std::int32_t kOutAt = 16;
    static constexpr std::int32_t kFloatLen = 12;
    static constexpr std::int32_t kMode = 2;

    static constexpr Arg kOut = Arg::outRegion(kOutAt, kFloatLen);
    static constexpr Arg kRounding = Arg::scalar(kMode);

    STAmount const amount{XRPAmount{1000}};
    Bytes const amountBytes = serialized(amount);
    std::int32_t const amountLen = static_cast<std::int32_t>(amountBytes.size());
    Arg const amountRegion = Arg::region(kAmountAt, amountLen);

    Bytes const result = [] {
        Bytes bytes(kFloatLen, 0xab);
        bytes[0] = 0x0d;
        bytes[1] = 0x0c;
        bytes[2] = 0x0b;
        bytes[3] = 0x0a;
        return bytes;
    }();

    [[nodiscard]] std::string
    watFor(Arg amountArg, Arg outArg, Arg modeArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "float_from_stamount",
            {amountArg, outArg, modeArg},
            {{.at = kAmountAt, .bytes = amountBytes}},
            answer);
    }
};

TEST_F(FloatFromSTAmountGuest, amount_and_mode_reach_host_in_order_and_float_comes_back)
{
    EXPECT_CALL(host, floatFromSTAmount(Eq(amount), kMode)).WillOnce(Return(result));

    auto const wat = watFor(amountRegion, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the float's first four bytes, little-endian";
}

}  // namespace xrpl::test
