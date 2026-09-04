#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>
#include <tx/wasm/fixtures/MockHostFunctions.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// Every input slice passes straight through to the host, unlike `invokeWithAccount`'s
// twenty-byte check or `parseUint64`'s eight: nothing here is validated, so there is no D
// axis. `x` and `y` carry different content, so a call that swapped them would fail to match.
struct FloatSubtractCall : HostContextTest
{
    Bytes const x{'s', 'u', 'b', '-', 'x'};
    Bytes const y{'s', 'u', 'b', '-', 'y', 'y'};
    std::int32_t const mode = 13;
};

TEST_F(FloatSubtractCall, OperandsAndModeAreForwardedResultIsWritten)
{
    Bytes const result{9, 8, 7};
    EXPECT_CALL(host, floatSubtract(BytesAre("sub-x"), BytesAre("sub-yy"), mode))
        .WillOnce(testing::Return(result));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatSubtract(bytesOf(x), bytesOf(y), mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_TRUE(out.holds(bytesOf(result)));
}

TEST_F(FloatSubtractCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatSubtract(BytesAre("sub-x"), BytesAre("sub-yy"), mode))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::FloatComputationError)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatSubtract(bytesOf(x), bytesOf(y), mode, out.slice()),
        hfErrorToInt(HostFunctionError::FloatComputationError));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(FloatSubtractCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, floatSubtract(BytesAre("sub-x"), BytesAre("sub-yy"), mode))
        .WillOnce(testing::Throw(std::runtime_error{"float subtract came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatSubtract(bytesOf(x), bytesOf(y), mode, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("float subtract came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("floatSubtract"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(FloatSubtractCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const result{9, 8, 7};
    EXPECT_CALL(host, floatSubtract(BytesAre("sub-x"), BytesAre("sub-yy"), mode))
        .WillOnce(testing::Return(result));

    OutRegion out{result.size() - 1};
    EXPECT_EQ(
        hostContext.floatSubtract(bytesOf(x), bytesOf(y), mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_FALSE(out.wasWritten());
}

// No length rule exists at this layer: a differently sized operand still reaches the host
// rather than being refused.
TEST_F(FloatSubtractCall, OddSizedOperandReachesHostUnchanged)
{
    Bytes const shortX{0x2a};
    EXPECT_CALL(host, floatSubtract(testing::_, BytesAre("sub-yy"), mode))
        .WillOnce(testing::Return(Bytes{1}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.floatSubtract(bytesOf(shortX), bytesOf(y), mode, out.slice()), 1);
}

}  // namespace xrpl::test
