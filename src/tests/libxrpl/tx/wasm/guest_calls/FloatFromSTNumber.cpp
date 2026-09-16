#include <xrpl/basics/Number.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
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
struct FloatFromSTNumberGuest : HostCallTest
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

TEST_F(FloatFromSTNumberGuest, NumberAndModeReachHostInOrderAndFloatComesBack)
{
    EXPECT_CALL(host, floatFromSTNumber(Eq(number), kMode)).WillOnce(Return(result));

    auto const wat = watFor(numberRegion, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the float's first four bytes, little-endian";
}

TEST_F(FloatFromSTNumberGuest, StatusIsTheFloatsLength)
{
    EXPECT_CALL(host, floatFromSTNumber(Eq(number), kMode)).WillOnce(Return(result));

    auto const wat = watFor(numberRegion, kOut, kRounding, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kFloatLen);
}

TEST_F(FloatFromSTNumberGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatFromSTNumber(Eq(number), kMode))
        .WillOnce(Return(std::unexpected(HostFunctionError::FloatComputationError)));

    auto const wat = watFor(numberRegion, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::FloatComputationError));
}

TEST_F(FloatFromSTNumberGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, floatFromSTNumber(Eq(number), kMode))
        .WillOnce(testing::Throw(std::runtime_error{"float from st number came apart"}));

    auto const outcome = callHost(watFor(numberRegion, kOut, kRounding));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("floatFromSTNumber"));
}

// The mantissa alone is not an `STNumber`. `HostContext`'s `parseST` catches `SerialIter`'s
// throw itself, so the refusal is an ordinary status and the host is never asked.
TEST_F(FloatFromSTNumberGuest, TruncatedNumberIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatFromSTNumber).Times(0);

    auto const wat = watFor(Arg::region(kNumberAt, 8), kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(FloatFromSTNumberGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatFromSTNumber).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, numberLen), kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatFromSTNumberGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatFromSTNumber).Times(0);

    auto const wat = watFor(Arg::region(-1, numberLen), kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(FloatFromSTNumberGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, floatFromSTNumber(Eq(number), kMode)).WillOnce(Return(result));

    auto const wat = watFor(numberRegion, Arg::outRegion(kOutAt, kFloatLen - 1), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(FloatFromSTNumberGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, floatFromSTNumber(Eq(number), kMode)).WillOnce(Return(result));

    auto const wat = watFor(numberRegion, Arg::outRegion(kOnePage, kFloatLen), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatFromSTNumberGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, floatFromSTNumber(Eq(number), kMode)).WillOnce(Return(result));

    auto const wat = watFor(numberRegion, Arg::outRegion(-1, kFloatLen), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
