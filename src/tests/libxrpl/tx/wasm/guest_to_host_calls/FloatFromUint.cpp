#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

using testing::Return;

// float_from_uint — a region and a rounding mode in, a float region out.
struct FloatFromUintGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kXAt = 0;
    static constexpr std::int32_t kOutAt = 16;
    static constexpr std::int32_t kUintLen = 8;
    static constexpr std::int32_t kFloatLen = 12;
    static constexpr std::int32_t kMode = 2;

    static constexpr std::uint64_t kValue = 0x0102'0304'0506'0708ULL;

    // Unlike `float_from_int`, `x` is a *region* holding the number as eight little-endian
    // bytes, and `HostContext`'s `parseUint64` refuses any other width.
    static constexpr Arg kX = Arg::region(kXAt, kUintLen);
    static constexpr Arg kOut = Arg::outRegion(kOutAt, kFloatLen);
    static constexpr Arg kRounding = Arg::scalar(kMode);

    // Every byte distinct, so a byte-order mistake would decode to a different number rather
    // than to the same one by coincidence.
    Bytes const xBytes{0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};

    Bytes const result = [] {
        Bytes bytes(kFloatLen, 0xab);
        bytes[0] = 0x0d;
        bytes[1] = 0x0c;
        bytes[2] = 0x0b;
        bytes[3] = 0x0a;
        return bytes;
    }();

    [[nodiscard]] std::string
    watFor(Arg xArg, Arg outArg, Arg modeArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "float_from_uint", {xArg, outArg, modeArg}, {{.at = kXAt, .bytes = xBytes}}, answer);
    }
};

TEST_F(FloatFromUintGuest, uint_bytes_and_mode_reach_host_in_order_and_float_comes_back)
{
    EXPECT_CALL(host, floatFromUint(kValue, kMode)).WillOnce(Return(result));

    auto const wat = watFor(kX, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the float's first four bytes, little-endian";
}

}  // namespace xrpl::test
