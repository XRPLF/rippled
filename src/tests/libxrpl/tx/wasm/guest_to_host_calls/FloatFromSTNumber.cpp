#include <xrpl/basics/Number.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>
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

// The wire form `STNumber(SerialIter&, SField const&)` expects: an eight-byte mantissa
// followed by a four-byte exponent. Built directly rather than through `STNumber::add`, which
// asserts its field is bound to `STI_NUMBER` — an assertion `sfGeneric` does not satisfy.
Bytes
serialized(std::int64_t mantissa, std::int32_t exponent)
{
    Serializer s;
    s.add64(mantissa);
    s.add32(exponent);
    return s.getData();
}

}  // namespace

// float_from_stnumber — a serialized `STNumber` region and a rounding mode in, a float region
// out.
struct FloatFromSTNumberGuest : GuestToHostCallTest
{
    static constexpr std::int32_t kNumberAt = 0;
    static constexpr std::int32_t kOutAt = 16;
    static constexpr std::int32_t kFloatLen = 12;
    static constexpr std::int32_t kMode = 2;
    static constexpr std::int64_t kMantissa = 123456789;
    static constexpr std::int32_t kExponent = -5;

    static constexpr Arg kOut = Arg::outRegion(kOutAt, kFloatLen);
    static constexpr Arg kRounding = Arg::scalar(kMode);

    STNumber const number{sfGeneric, Number{kMantissa, kExponent}};
    Bytes const numberBytes = serialized(kMantissa, kExponent);
    std::int32_t const numberLen = static_cast<std::int32_t>(numberBytes.size());
    Arg const numberRegion = Arg::region(kNumberAt, numberLen);

    Bytes const result = [] {
        Bytes bytes(kFloatLen, 0xab);
        bytes[0] = 0x0d;
        bytes[1] = 0x0c;
        bytes[2] = 0x0b;
        bytes[3] = 0x0a;
        return bytes;
    }();

    [[nodiscard]] std::string
    watFor(Arg numberArg, Arg outArg, Arg modeArg, Answer answer = Answer::WrittenBytes) const
    {
        return hostCallWat(
            "float_from_stnumber",
            {numberArg, outArg, modeArg},
            {{.at = kNumberAt, .bytes = numberBytes}},
            answer);
    }
};

TEST_F(FloatFromSTNumberGuest, number_and_mode_reach_host_in_order_and_float_comes_back)
{
    EXPECT_CALL(host, floatFromSTNumber(Eq(number), kMode)).WillOnce(Return(result));

    auto const wat = watFor(numberRegion, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the float's first four bytes, little-endian";
}

}  // namespace xrpl::test
