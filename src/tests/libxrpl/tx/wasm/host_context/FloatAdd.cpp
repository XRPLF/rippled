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
struct FloatAddCall : HostContextTest
{
    Bytes const x{'a', 'd', 'd', '-', 'x'};
    Bytes const y{'a', 'd', 'd', '-', 'y', 'y'};
    std::int32_t const mode = 7;
};

TEST_F(FloatAddCall, OperandsAndModeAreForwardedResultIsWritten)
{
    Bytes const result{9, 8, 7};
    EXPECT_CALL(host, floatAdd(BytesAre("add-x"), BytesAre("add-yy"), mode))
        .WillOnce(testing::Return(result));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatAdd(bytesOf(x), bytesOf(y), mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_TRUE(out.holds(bytesOf(result)));
}

TEST_F(FloatAddCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatAdd(BytesAre("add-x"), BytesAre("add-yy"), mode))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::FloatComputationError)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatAdd(bytesOf(x), bytesOf(y), mode, out.slice()),
        hfErrorToInt(HostFunctionError::FloatComputationError));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(FloatAddCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, floatAdd(BytesAre("add-x"), BytesAre("add-yy"), mode))
        .WillOnce(testing::Throw(std::runtime_error{"float add came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatAdd(bytesOf(x), bytesOf(y), mode, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("float add came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("floatAdd"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(FloatAddCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const result{9, 8, 7};
    EXPECT_CALL(host, floatAdd(BytesAre("add-x"), BytesAre("add-yy"), mode))
        .WillOnce(testing::Return(result));

    OutRegion out{result.size() - 1};
    EXPECT_EQ(
        hostContext.floatAdd(bytesOf(x), bytesOf(y), mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_FALSE(out.wasWritten());
}

// No length rule exists at this layer: a differently sized operand still reaches the host
// rather than being refused.
TEST_F(FloatAddCall, OddSizedOperandReachesHostUnchanged)
{
    Bytes const shortX{0x2a};
    EXPECT_CALL(host, floatAdd(testing::_, BytesAre("add-yy"), mode))
        .WillOnce(testing::Return(Bytes{1}));

    OutRegion out{32};
    EXPECT_EQ(hostContext.floatAdd(bytesOf(shortX), bytesOf(y), mode, out.slice()), 1);
}

}  // namespace xrpl::test
