#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/HostContextFixture.h>
#include <tx/wasm/MockHostFunctions.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// Every input slice passes straight through to the host, unlike `invokeWithAccount`'s
// twenty-byte check or `parseUint64`'s eight: nothing here is validated, so there is no D
// axis. `x` and `y` carry different content, so a call that swapped them would fail to match.
struct FloatMultiplyCall : HostContextTest
{
    Bytes const x{'m', 'u', 'l', '-', 'x'};
    Bytes const y{'m', 'u', 'l', '-', 'y', 'y'};
    std::int32_t const mode = 21;
};

TEST_F(FloatMultiplyCall, OperandsAndModeAreForwardedResultIsWritten)
{
    Bytes const result{9, 8, 7};
    EXPECT_CALL(host, floatMultiply(BytesAre("mul-x"), BytesAre("mul-yy"), mode))
        .WillOnce(testing::Return(result));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatMultiply(bytesOf(x), bytesOf(y), mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_TRUE(out.holds(bytesOf(result)));
}

TEST_F(FloatMultiplyCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatMultiply(BytesAre("mul-x"), BytesAre("mul-yy"), mode))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::FloatComputationError)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatMultiply(bytesOf(x), bytesOf(y), mode, out.slice()),
        hfErrorToInt(HostFunctionError::FloatComputationError));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(FloatMultiplyCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, floatMultiply(BytesAre("mul-x"), BytesAre("mul-yy"), mode))
        .WillOnce(testing::Throw(std::runtime_error{"float multiply came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatMultiply(bytesOf(x), bytesOf(y), mode, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("float multiply came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("floatMultiply"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(FloatMultiplyCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const result{9, 8, 7};
    EXPECT_CALL(host, floatMultiply(BytesAre("mul-x"), BytesAre("mul-yy"), mode))
        .WillOnce(testing::Return(result));

    OutRegion out{result.size() - 1};
    EXPECT_EQ(
        hostContext.floatMultiply(bytesOf(x), bytesOf(y), mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_FALSE(out.wasWritten());
}

// No length rule exists at this layer: a differently sized operand still reaches the host
// rather than being refused.
TEST_F(FloatMultiplyCall, OddSizedOperandReachesHostUnchanged)
{
    Bytes const shortX{0x2a};
    EXPECT_CALL(host, floatMultiply(testing::_, BytesAre("mul-yy"), mode))
        .WillOnce(testing::Return(Bytes{1}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.floatMultiply(bytesOf(shortX), bytesOf(y), mode, out.slice()), 1);
}

}  // namespace xrpl::test
