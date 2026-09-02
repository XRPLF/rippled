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
// axis. `n` and `mode` carry different values, so a call that swapped them would fail to
// match.
struct FloatPowerCall : HostContextTest
{
    Bytes const x{'p', 'o', 'w', '-', 'x'};
    std::int32_t const n = 4;
    std::int32_t const mode = 22;
};

TEST_F(FloatPowerCall, OperandNAndModeAreForwardedResultIsWritten)
{
    Bytes const result{4, 5, 6};
    EXPECT_CALL(host, floatPower(BytesAre("pow-x"), n, mode)).WillOnce(testing::Return(result));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatPower(bytesOf(x), n, mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_TRUE(out.holds(bytesOf(result)));
}

TEST_F(FloatPowerCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatPower(BytesAre("pow-x"), n, mode))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::FloatComputationError)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatPower(bytesOf(x), n, mode, out.slice()),
        hfErrorToInt(HostFunctionError::FloatComputationError));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(FloatPowerCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, floatPower(BytesAre("pow-x"), n, mode))
        .WillOnce(testing::Throw(std::runtime_error{"float power came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatPower(bytesOf(x), n, mode, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("float power came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("floatPower"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(FloatPowerCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const result{4, 5, 6};
    EXPECT_CALL(host, floatPower(BytesAre("pow-x"), n, mode)).WillOnce(testing::Return(result));

    OutRegion out{result.size() - 1};
    EXPECT_EQ(
        hostContext.floatPower(bytesOf(x), n, mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_FALSE(out.wasWritten());
}

// No length rule exists at this layer: a differently sized operand still reaches the host
// rather than being refused.
TEST_F(FloatPowerCall, OddSizedOperandReachesHostUnchanged)
{
    Bytes const shortX{0x2a};
    EXPECT_CALL(host, floatPower(testing::_, n, mode)).WillOnce(testing::Return(Bytes{1}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.floatPower(bytesOf(shortX), n, mode, out.slice()), 1);
}

// `mode` and `n` validate nothing at this layer and cross verbatim, including values with no
// real meaning. Worth pinning once across the float family rather than in every file.
TEST_F(FloatPowerCall, ModeAndNAreForwardedVerbatim)
{
    std::int32_t const nonsenseN = -999;
    std::int32_t const nonsenseMode = 424242;
    Bytes const result{1};
    EXPECT_CALL(host, floatPower(BytesAre("pow-x"), nonsenseN, nonsenseMode))
        .WillOnce(testing::Return(result));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatPower(bytesOf(x), nonsenseN, nonsenseMode, out.slice()),
        static_cast<std::int32_t>(result.size()));
}

}  // namespace xrpl::test
