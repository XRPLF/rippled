#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// `x` arrives as a wasm scalar, not as bytes to decode, so there is nothing here to get wrong
// about its shape - no D axis.
struct FloatFromIntCall : HostContextTest
{
    std::int64_t const x = 123456789;
    std::int32_t const mode = 1;
};

TEST_F(FloatFromIntCall, ValueAndModeAreForwardedResultIsWritten)
{
    Bytes const result{1, 2, 3};
    EXPECT_CALL(host, floatFromInt(x, mode)).WillOnce(testing::Return(result));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromInt(x, mode, out.slice()), static_cast<std::int32_t>(result.size()));
    EXPECT_TRUE(out.holds(bytesOf(result)));
}

// `mode` is forwarded verbatim: this layer validates nothing about it, so a nonsense value
// still reaches the host unchanged.
TEST_F(FloatFromIntCall, ModeIsForwardedVerbatim)
{
    std::int32_t const nonsenseMode = -12345;
    Bytes const result{1};
    EXPECT_CALL(host, floatFromInt(x, nonsenseMode)).WillOnce(testing::Return(result));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromInt(x, nonsenseMode, out.slice()),
        static_cast<std::int32_t>(result.size()));
}

TEST_F(FloatFromIntCall, HostErrorBecomesContractReturnValue)
{
    EXPECT_CALL(host, floatFromInt(x, mode))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::FloatComputationError)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromInt(x, mode, out.slice()),
        hfErrorToInt(HostFunctionError::FloatComputationError));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(FloatFromIntCall, HostExceptionBecomesInternalFatalAndIsLogged)
{
    EXPECT_CALL(host, floatFromInt(x, mode))
        .WillOnce(testing::Throw(std::runtime_error{"float from int came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromInt(x, mode, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("float from int came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("floatFromInt"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(FloatFromIntCall, ShortOutRegionWritesNothingAndReturnsTrueLength)
{
    Bytes const result{1, 2, 3};
    EXPECT_CALL(host, floatFromInt(x, mode)).WillOnce(testing::Return(result));

    OutRegion out{result.size() - 1};
    EXPECT_EQ(
        hostContext.floatFromInt(x, mode, out.slice()), static_cast<std::int32_t>(result.size()));
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test
