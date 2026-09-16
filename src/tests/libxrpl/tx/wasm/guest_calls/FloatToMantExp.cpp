#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostCallFixture.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>
#include <tx/wasm/fixtures/WasmRun.h>

#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace xrpl::test {

using testing::Return;

// float_to_mant_exp — a float region in, and the only call in the ABI that writes *two*
// output regions.
//
// `hostCallWat` serves at most one out region, so this file builds its own module. Rust's
// `tests/host_calls.rs` covers the two regions' bounds and fit against a fake host; what only
// this layer can show is that `CxxHost`'s forward puts the `FloatPair`'s halves into the
// guest's regions the right way round.
struct FloatToMantExpGuest : HostCallTest
{
    static constexpr std::int32_t kXAt = 0;
    static constexpr std::int32_t kMantissaAt = 16;
    static constexpr std::int32_t kExponentAt = 32;
    static constexpr std::int32_t kFloatLen = 12;
    static constexpr std::int32_t kMantissaLen = 8;
    static constexpr std::int32_t kExponentLen = 4;
    static constexpr std::string_view kXText = "float-mantex";

    static constexpr Arg kX = Arg::region(kXAt, kFloatLen);
    static constexpr Arg kMantissaOut = Arg::outRegion(kMantissaAt, kMantissaLen);
    static constexpr Arg kExponentOut = Arg::outRegion(kExponentAt, kExponentLen);

    // The mantissa's low four bytes and the exponent read differently, so a pair written into
    // each other's region fails rather than passing.
    static constexpr FloatPair kPair{0x1122'3344'0a0b'0c0dLL, -5};

    Bytes const xBytes{kXText.begin(), kXText.end()};

    // `readAt` is where the guest reads its answer from once the call succeeds; without one
    // the contract returns the call's own status.
    [[nodiscard]] std::string
    watFor(
        Arg xArg,
        Arg mantissaArg,
        Arg exponentArg,
        std::optional<std::int32_t> readAt = std::nullopt) const
    {
        auto const answerExpr = readAt ? std::format("(i32.load (i32.const {}))", *readAt)
                                       : std::string{"(local.get $n)"};

        return std::format(
            R"wat(
(module
  (import "host_lib" "float_to_mant_exp"
    (func $f (param i32 i32 i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const {0}) "{1}")
  (func (export "escrow_finish") (result i32)
    (local $n i32)
    (local.set $n (call $f
      (i32.const {2}) (i32.const {3})
      (i32.const {4}) (i32.const {5})
      (i32.const {6}) (i32.const {7})))
    (if (result i32) (i32.lt_s (local.get $n) (i32.const 0))
      (then (local.get $n))
      (else {8}))))
)wat",
            kXAt,
            watEscaped(xBytes),
            xArg.value,
            xArg.len,
            mantissaArg.value,
            mantissaArg.len,
            exponentArg.value,
            exponentArg.len,
            answerExpr);
    }
};

TEST_F(FloatToMantExpGuest, OperandReachesHostAndMantissaLandsInItsRegion)
{
    EXPECT_CALL(host, floatToMantExp(BytesAre(kXText))).WillOnce(Return(kPair));

    auto const wat = watFor(kX, kMantissaOut, kExponentOut, kMantissaAt);
    EXPECT_EQ(hostAnswer(wat), 0x0a0b0c0d) << "the mantissa's first four bytes, little-endian";
}

TEST_F(FloatToMantExpGuest, ExponentLandsInItsOwnRegion)
{
    EXPECT_CALL(host, floatToMantExp(BytesAre(kXText))).WillOnce(Return(kPair));

    auto const wat = watFor(kX, kMantissaOut, kExponentOut, kExponentAt);
    EXPECT_EQ(hostAnswer(wat), kPair.second);
}

// Both widths are the ABI's rather than the guest's, so the status is their sum and not
// either length the guest declared.
TEST_F(FloatToMantExpGuest, StatusIsBothWidthsSummed)
{
    EXPECT_CALL(host, floatToMantExp(BytesAre(kXText))).WillOnce(Return(kPair));

    auto const wat = watFor(kX, kMantissaOut, kExponentOut);
    EXPECT_EQ(hostAnswer(wat), kMantissaLen + kExponentLen);
}

TEST_F(FloatToMantExpGuest, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatToMantExp(BytesAre(kXText)))
        .WillOnce(Return(std::unexpected(HostFunctionError::FloatComputationError)));

    auto const wat = watFor(kX, kMantissaOut, kExponentOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::FloatComputationError));
}

TEST_F(FloatToMantExpGuest, HostExceptionStopsTheRunAndIsLogged)
{
    EXPECT_CALL(host, floatToMantExp(BytesAre(kXText)))
        .WillOnce(testing::Throw(std::runtime_error{"float to mant exp came apart"}));

    auto const outcome = callHost(watFor(kX, kMantissaOut, kExponentOut));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_THAT(logged(), testing::HasSubstr("floatToMantExp"));
}

TEST_F(FloatToMantExpGuest, InputRegionPastMemoryIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatToMantExp).Times(0);

    auto const wat = watFor(Arg::region(kOnePage, kFloatLen), kMantissaOut, kExponentOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::PointerOutOfBounds));
}

TEST_F(FloatToMantExpGuest, NegativeInputPointerIsRefusedWithoutAskingHost)
{
    EXPECT_CALL(host, floatToMantExp).Times(0);

    auto const wat = watFor(Arg::region(-1, kFloatLen), kMantissaOut, kExponentOut);
    EXPECT_EQ(hostAnswer(wat), hfErrorToInt(HostFunctionError::InvalidParams));
}

}  // namespace xrpl::test
