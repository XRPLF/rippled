#include <xrpl/protocol/STAmount.h>
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
struct FloatFromSTAmountGuest : HostCallTest
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

TEST_F(FloatFromSTAmountGuest, AmountAndModeReachHostInOrderAndFloatComesBack)
{
    EXPECT_CALL(host, floatFromSTAmount(Eq(amount), kMode)).WillOnce(Return(result));

    auto const wat = watFor(amountRegion, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the float's first four bytes, little-endian";
}

TEST_F(FloatFromSTAmountGuest, StatusIsTheFloatsLength)
{
    EXPECT_CALL(host, floatFromSTAmount(Eq(amount), kMode)).WillOnce(Return(result));

    auto const wat = watFor(amountRegion, kOut, kRounding, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kFloatLen);
}

TEST_F(FloatFromSTAmountGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatFromSTAmount(Eq(amount), kMode))
        .WillOnce(Return(std::unexpected(HostFunctionError::FloatComputationError)));

    auto const wat = watFor(amountRegion, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::FloatComputationError));
}

TEST_F(FloatFromSTAmountGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, floatFromSTAmount(Eq(amount), kMode))
        .WillOnce(testing::Throw(std::runtime_error{"float from st amount came apart"}));

    auto const outcome = callHost(watFor(amountRegion, kOut, kRounding));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("floatFromSTAmount"));
}

// Three bytes of a well-formed amount are still not one. `HostContext`'s `parseST` catches
// `SerialIter`'s throw itself, so the refusal is an ordinary status and the host is never
// asked.
TEST_F(FloatFromSTAmountGuest, TruncatedAmountIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatFromSTAmount).Times(0);

    auto const wat = watFor(Arg::region(kAmountAt, 3), kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(FloatFromSTAmountGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatFromSTAmount).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, amountLen), kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatFromSTAmountGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatFromSTAmount).Times(0);

    auto const wat = watFor(Arg::region(-1, amountLen), kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(FloatFromSTAmountGuest, OutRegionOneByteShortIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, floatFromSTAmount(Eq(amount), kMode)).WillOnce(Return(result));

    auto const wat = watFor(amountRegion, Arg::outRegion(kOutAt, kFloatLen - 1), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(FloatFromSTAmountGuest, OutRegionPastMemoryIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, floatFromSTAmount(Eq(amount), kMode)).WillOnce(Return(result));

    auto const wat = watFor(amountRegion, Arg::outRegion(kOnePage, kFloatLen), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatFromSTAmountGuest, NegativeOutPointerIsRefusedAfterAskingHost)
{
    EXPECT_CALL(host, floatFromSTAmount(Eq(amount), kMode)).WillOnce(Return(result));

    auto const wat = watFor(amountRegion, Arg::outRegion(-1, kFloatLen), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
