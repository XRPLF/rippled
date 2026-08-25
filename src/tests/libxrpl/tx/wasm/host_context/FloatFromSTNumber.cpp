#include <xrpl/basics/Number.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/HostContextFixture.h>

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
