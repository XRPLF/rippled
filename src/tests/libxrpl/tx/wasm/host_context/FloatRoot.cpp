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
// axis. `n` and `mode` carry different values, so a call that swapped them would fail to
// match.
struct FloatRootCall : HostContextTest
{
    Bytes const x{'r', 'o', 'o', 't', '-', 'x'};
    std::int32_t const n = 3;
    std::int32_t const mode = 11;
};

TEST_F(FloatRootCall, OperandNAndModeAreForwardedResultIsWritten)
{
    Bytes const result{4, 5, 6};
    EXPECT_CALL(host, floatRoot(BytesAre("root-x"), n, mode)).WillOnce(testing::Return(result));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatRoot(bytesOf(x), n, mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_TRUE(out.holds(bytesOf(result)));
}

TEST_F(FloatRootCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatRoot(BytesAre("root-x"), n, mode))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::FloatComputationError)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatRoot(bytesOf(x), n, mode, out.slice()),
        hfErrorToInt(HostFunctionError::FloatComputationError));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(FloatRootCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, floatRoot(BytesAre("root-x"), n, mode))
        .WillOnce(testing::Throw(std::runtime_error{"float root came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatRoot(bytesOf(x), n, mode, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("float root came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("floatRoot"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(FloatRootCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const result{4, 5, 6};
    EXPECT_CALL(host, floatRoot(BytesAre("root-x"), n, mode)).WillOnce(testing::Return(result));

    OutRegion out{result.size() - 1};
    EXPECT_EQ(
        hostContext.floatRoot(bytesOf(x), n, mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_FALSE(out.wasWritten());
}

// No length rule exists at this layer: a differently sized operand still reaches the host
// rather than being refused.
TEST_F(FloatRootCall, OddSizedOperandReachesHostUnchanged)
{
    Bytes const shortX{0x2a};
    EXPECT_CALL(host, floatRoot(testing::_, n, mode)).WillOnce(testing::Return(Bytes{1}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.floatRoot(bytesOf(shortX), n, mode, out.slice()), 1);
}

}  // namespace xrpl::test
