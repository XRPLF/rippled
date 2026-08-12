#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/HostContextFixture.h>
#include <tx/wasm/MockHostFunctions.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// The input slice passes straight through to the host, unlike `invokeWithAccount`'s twenty-byte
// check or `parseUint64`'s eight: nothing here is validated, so there is no D axis.
struct FloatToIntCall : HostContextTest
{
    Bytes const x{'t', 'o', 'i', 'n', 't'};
    std::int32_t const mode = 3;
};

TEST_F(FloatToIntCall, OperandAndModeAreForwardedResultWrittenAsLittleEndianBytes)
{
    std::int64_t const value = -123456789;
    EXPECT_CALL(host, floatToInt(BytesAre("toint"), mode)).WillOnce(testing::Return(value));

    OutRegion out{32};
    EXPECT_EQ(hostContext.floatToInt(bytesOf(x), mode, out.slice()), 8);
    EXPECT_TRUE(out.holds(bytesOf(bytesOfScalar(value))));
}

TEST_F(FloatToIntCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatToInt(BytesAre("toint"), mode))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::FloatComputationError)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatToInt(bytesOf(x), mode, out.slice()),
        hfErrorToInt(HostFunctionError::FloatComputationError));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(FloatToIntCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, floatToInt(BytesAre("toint"), mode))
        .WillOnce(testing::Throw(std::runtime_error{"float to int came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatToInt(bytesOf(x), mode, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("float to int came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("floatToInt"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(FloatToIntCall, SevenByteOutRegionWritesNothingAndReturnsTrueLength)
{
    std::int64_t const value = 42;
    EXPECT_CALL(host, floatToInt(BytesAre("toint"), mode)).WillOnce(testing::Return(value));

    OutRegion out{7};
    EXPECT_EQ(hostContext.floatToInt(bytesOf(x), mode, out.slice()), 8);
    EXPECT_FALSE(out.wasWritten());
}

// No length rule exists at this layer: a differently sized operand still reaches the host
// rather than being refused.
TEST_F(FloatToIntCall, OddSizedOperandReachesHostUnchanged)
{
    Bytes const oddX{0x2a};
    EXPECT_CALL(host, floatToInt(testing::_, mode)).WillOnce(testing::Return(std::int64_t{7}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.floatToInt(bytesOf(oddX), mode, out.slice()), 8);
}

}  // namespace xrpl::test
