#include <xrpl/basics/Number.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

namespace {

// The wire form `STNumber(SerialIter&, SField const&)` expects: an eight-byte mantissa
// followed by a four-byte exponent. Built directly rather than through `STNumber::add`, which
// asserts its field is bound to `STI_NUMBER` - an assertion `sfGeneric` does not satisfy.
Bytes
serialized(std::int64_t mantissa, std::int32_t exponent)
{
    Serializer s;
    s.add64(mantissa);
    s.add32(exponent);
    return s.getData();
}

}  // namespace

// The only file exercising `parseST<STNumber>`. A malformed buffer throws inside `STNumber`'s
// deserializing constructor; `parseST` catches that itself, so the host is never asked - unlike
// a `guarded`-caught throw from the host's own body.
struct FloatFromSTNumberCall : HostContextTest
{
    std::int64_t const mantissa = 123456789;
    std::int32_t const exponent = -5;
    STNumber const number{sfGeneric, Number{mantissa, exponent}};
    Bytes const wireBytes = serialized(mantissa, exponent);
    std::int32_t const mode = 1;
};

TEST_F(FloatFromSTNumberCall, SerializedNumberDecodesToValueHostIsAskedFor)
{
    Bytes const result{1, 2, 3};
    EXPECT_CALL(host, floatFromSTNumber(testing::Eq(number), mode))
        .WillOnce(testing::Return(result));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromSTNumber(bytesOf(wireBytes), mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_TRUE(out.holds(bytesOf(result)));
}

TEST_F(FloatFromSTNumberCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatFromSTNumber(testing::Eq(number), mode))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::FloatComputationError)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromSTNumber(bytesOf(wireBytes), mode, out.slice()),
        hfErrorToInt(HostFunctionError::FloatComputationError));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(FloatFromSTNumberCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, floatFromSTNumber(testing::Eq(number), mode))
        .WillOnce(testing::Throw(std::runtime_error{"float from st number came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromSTNumber(bytesOf(wireBytes), mode, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("float from st number came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("floatFromSTNumber"));
}

// `parseST` catches its own failure: a malformed buffer never reaches the host at all.
TEST_F(FloatFromSTNumberCall, MalformedBytesAreRefusedWithoutAskingHost)
{
    Bytes const malformedBytes{0xff, 0xff, 0xff};
    EXPECT_CALL(host, floatFromSTNumber).Times(0);

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromSTNumber(bytesOf(malformedBytes), mode, out.slice()),
        hfErrorToInt(HostFunctionError::InvalidParams));
}

// Decoding is where the rounding happens, so the guest's mode has to be installed before it.
struct FloatFromSTNumberRounding : HostContextTest
{
    // Declared first so the range is in force while the expectations below are built.
    NumberMantissaScaleGuard const scale{MantissaRange::MantissaScale::Small};

    // Seventeen digits against a sixteen-digit range: normalizing drops the last one and the
    // mode decides its fate. A dropped `7` rounds up under `ToNearest`, a dropped `3` down, so
    // each case below disagrees with `ToNearest` and fails if the decode does not honour the
    // mode it was given.
    std::int32_t const exponent = 0;
    Bytes const dropsSeven = serialized(12'345'678'901'234'567, exponent);
    Bytes const dropsThree = serialized(12'345'678'901'234'563, exponent);

    STNumber const truncated{sfGeneric, Number{1'234'567'890'123'456, 1}};
    STNumber const raised{sfGeneric, Number{1'234'567'890'123'457, 1}};

    void
    expectDecodedAs(Bytes const& wire, Number::RoundingMode mode, STNumber const& expected)
    {
        auto const asInt = static_cast<std::int32_t>(mode);
        auto const result = Bytes{1, 2, 3};
        EXPECT_CALL(host, floatFromSTNumber(testing::Eq(expected), asInt))
            .WillOnce(testing::Return(result));

        auto out = OutRegion{32};
        EXPECT_EQ(
            hostContext.floatFromSTNumber(bytesOf(wire), asInt, out.slice()),
            static_cast<std::int32_t>(result.size()));
    }
};

TEST_F(FloatFromSTNumberRounding, towards_zero_truncates_instead_of_rounding_to_nearest)
{
    expectDecodedAs(dropsSeven, Number::RoundingMode::TowardsZero, truncated);
}

TEST_F(FloatFromSTNumberRounding, upward_rounds_away_instead_of_rounding_to_nearest)
{
    expectDecodedAs(dropsThree, Number::RoundingMode::Upward, raised);
}

TEST_F(FloatFromSTNumberRounding, downward_truncates_instead_of_rounding_to_nearest)
{
    expectDecodedAs(dropsSeven, Number::RoundingMode::Downward, truncated);
}

TEST_F(FloatFromSTNumberRounding, to_nearest_is_unchanged)
{
    expectDecodedAs(dropsSeven, Number::RoundingMode::ToNearest, raised);
    expectDecodedAs(dropsThree, Number::RoundingMode::ToNearest, truncated);
}

TEST_F(FloatFromSTNumberRounding, an_invalid_mode_still_reaches_the_host)
{
    constexpr auto kNotAMode = std::int32_t{99};
    EXPECT_CALL(host, floatFromSTNumber(testing::_, kNotAMode))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::FloatInputMalformed)));

    auto out = OutRegion{32};
    EXPECT_EQ(
        hostContext.floatFromSTNumber(bytesOf(dropsSeven), kNotAMode, out.slice()),
        hfErrorToInt(HostFunctionError::FloatInputMalformed));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(FloatFromSTNumberCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const result{1, 2, 3};
    EXPECT_CALL(host, floatFromSTNumber(testing::Eq(number), mode))
        .WillOnce(testing::Return(result));

    OutRegion out{result.size() - 1};
    EXPECT_EQ(
        hostContext.floatFromSTNumber(bytesOf(wireBytes), mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test
