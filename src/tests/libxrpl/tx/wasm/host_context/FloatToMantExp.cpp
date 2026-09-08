#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/HostContextFixture.h>
#include <tx/wasm/MockHostFunctions.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// The input slice passes straight through to the host, unlike `invokeWithAccount`'s twenty-byte
// check or `parseUint64`'s eight: nothing here is validated, so there is no D axis. The two out
// regions are each checked and written independently; the return is their summed true length.
struct FloatToMantExpCall : HostContextTest
{
    Bytes const x{'m', 'a', 'n', 't', 'e', 'x', 'p'};
    std::int64_t const mantissa = 0x0102'0304'0506'0708LL;
    std::int32_t const exponent = -5;
    FloatPair const pair{mantissa, exponent};
};

TEST_F(FloatToMantExpCall, OperandIsForwardedMantissaAndExponentWrittenAsLittleEndianBytes)
{
    EXPECT_CALL(host, floatToMantExp(BytesAre("mantexp"))).WillOnce(testing::Return(pair));

    OutRegion mantissaOut{8};
    OutRegion exponentOut{4};
    EXPECT_EQ(hostContext.floatToMantExp(bytesOf(x), mantissaOut.slice(), exponentOut.slice()), 12);
    EXPECT_TRUE(mantissaOut.holds(bytesOf(bytesOfScalar(mantissa))));
    EXPECT_TRUE(exponentOut.holds(bytesOf(bytesOfScalar(exponent))));
}

TEST_F(FloatToMantExpCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatToMantExp(BytesAre("mantexp")))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::FloatComputationError)));

    OutRegion mantissaOut{8};
    OutRegion exponentOut{4};
    EXPECT_EQ(
        hostContext.floatToMantExp(bytesOf(x), mantissaOut.slice(), exponentOut.slice()),
        hfErrorToInt(HostFunctionError::FloatComputationError));
    EXPECT_FALSE(mantissaOut.wasWritten());
    EXPECT_FALSE(exponentOut.wasWritten());
}

TEST_F(FloatToMantExpCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, floatToMantExp(BytesAre("mantexp")))
        .WillOnce(testing::Throw(std::runtime_error{"float to mant exp came apart"}));

    OutRegion mantissaOut{8};
    OutRegion exponentOut{4};
    EXPECT_EQ(
        hostContext.floatToMantExp(bytesOf(x), mantissaOut.slice(), exponentOut.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("float to mant exp came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("floatToMantExp"));
}

// Each region is checked independently: a short mantissa region does not stop the exponent
// from being written, and the sum still counts the mantissa's true length.
TEST_F(FloatToMantExpCall, ShortMantissaRegionWritesNothingThereSumStillCountsIt)
{
    EXPECT_CALL(host, floatToMantExp(BytesAre("mantexp"))).WillOnce(testing::Return(pair));

    OutRegion mantissaOut{7};
    OutRegion exponentOut{4};
    EXPECT_EQ(hostContext.floatToMantExp(bytesOf(x), mantissaOut.slice(), exponentOut.slice()), 12);
    EXPECT_FALSE(mantissaOut.wasWritten());
    EXPECT_TRUE(exponentOut.holds(bytesOf(bytesOfScalar(exponent))));
}

TEST_F(FloatToMantExpCall, ShortExponentRegionWritesNothingThereSumStillCountsIt)
{
    EXPECT_CALL(host, floatToMantExp(BytesAre("mantexp"))).WillOnce(testing::Return(pair));

    OutRegion mantissaOut{8};
    OutRegion exponentOut{3};
    EXPECT_EQ(hostContext.floatToMantExp(bytesOf(x), mantissaOut.slice(), exponentOut.slice()), 12);
    EXPECT_TRUE(mantissaOut.holds(bytesOf(bytesOfScalar(mantissa))));
    EXPECT_FALSE(exponentOut.wasWritten());
}

// No length rule exists at this layer: a differently sized operand still reaches the host
// rather than being refused.
TEST_F(FloatToMantExpCall, OddSizedOperandReachesHostUnchanged)
{
    Bytes const oddX{0x2a};
    EXPECT_CALL(host, floatToMantExp(testing::_)).WillOnce(testing::Return(pair));

    OutRegion mantissaOut{8};
    OutRegion exponentOut{4};
    EXPECT_EQ(
        hostContext.floatToMantExp(bytesOf(oddX), mantissaOut.slice(), exponentOut.slice()), 12);
}

}  // namespace xrpl::test
