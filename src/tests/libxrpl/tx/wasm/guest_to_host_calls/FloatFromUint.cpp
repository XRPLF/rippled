#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/GuestToHostCallFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>
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

TEST_F(FloatFromUintGuest, status_is_the_floats_length)
{
    EXPECT_CALL(host, floatFromUint(kValue, kMode)).WillOnce(Return(result));

    auto const wat = watFor(kX, kOut, kRounding, Answer::Status);
    EXPECT_EQ(hostAnswer(wat), kFloatLen);
}

TEST_F(FloatFromUintGuest, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, floatFromUint(kValue, kMode))
        .WillOnce(Return(std::unexpected(HostFunctionError::FloatComputationError)));

    auto const wat = watFor(kX, kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::FloatComputationError));
}

TEST_F(FloatFromUintGuest, host_exception_stops_the_run_and_is_logged)
{
    EXPECT_CALL(host, floatFromUint(kValue, kMode))
        .WillOnce(testing::Throw(std::runtime_error{"float from uint came apart"}));

    auto const outcome = run(watFor(kX, kOut, kRounding));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("floatFromUint"));
}

TEST_F(FloatFromUintGuest, operand_of_the_wrong_length_is_refused_without_asking_host)
{
    EXPECT_CALL(host, floatFromUint).Times(0);

    auto const wat = watFor(Arg::region(kXAt, kUintLen - 1), kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(FloatFromUintGuest, input_region_past_memory_is_refused_without_asking_host)
{
    EXPECT_CALL(host, floatFromUint).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kUintLen), kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatFromUintGuest, negative_input_pointer_is_refused_without_asking_host)
{
    EXPECT_CALL(host, floatFromUint).Times(0);

    auto const wat = watFor(Arg::region(-1, kUintLen), kOut, kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

TEST_F(FloatFromUintGuest, out_region_one_byte_short_is_refused_after_asking_host)
{
    EXPECT_CALL(host, floatFromUint(kValue, kMode)).WillOnce(Return(result));

    auto const wat = watFor(kX, Arg::outRegion(kOutAt, kFloatLen - 1), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::BufferTooSmall));
}

TEST_F(FloatFromUintGuest, out_region_past_memory_is_refused_after_asking_host)
{
    EXPECT_CALL(host, floatFromUint(kValue, kMode)).WillOnce(Return(result));

    auto const wat = watFor(kX, Arg::outRegion(kOnePage, kFloatLen), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatFromUintGuest, negative_out_pointer_is_refused_after_asking_host)
{
    EXPECT_CALL(host, floatFromUint(kValue, kMode)).WillOnce(Return(result));

    auto const wat = watFor(kX, Arg::outRegion(-1, kFloatLen), kRounding);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
