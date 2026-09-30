#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/HostContextFixture.h>

#include <cstdint>
#include <expected>
#include <stdexcept>

namespace xrpl::test {

// `mantissa`, `exponent` and `mode` all arrive as wasm scalars, not as bytes to decode, so
// there is nothing here to get wrong about their shape - no D axis.
struct FloatFromMantExpCall : HostContextTest
{
    std::int64_t const mantissa = 123456789;
    std::int32_t const exponent = -5;
    std::int32_t const mode = 1;
};

TEST_F(FloatFromMantExpCall, mantissa_exponent_and_mode_are_forwarded_result_is_written)
{
    Bytes const result{1, 2, 3};
    EXPECT_CALL(host, floatFromMantExp(mantissa, exponent, mode)).WillOnce(testing::Return(result));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromMantExp(mantissa, exponent, mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_TRUE(out.holds(bytesOf(result)));
}

TEST_F(FloatFromMantExpCall, host_error_becomes_contract_return_value)
{
    EXPECT_CALL(host, floatFromMantExp(mantissa, exponent, mode))
        .WillOnce(testing::Return(std::unexpected(HostFunctionError::FloatComputationError)));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromMantExp(mantissa, exponent, mode, out.slice()),
        hfErrorToInt(HostFunctionError::FloatComputationError));
    EXPECT_FALSE(out.wasWritten());
}

TEST_F(FloatFromMantExpCall, host_exception_becomes_internal_fatal_and_is_logged)
{
    EXPECT_CALL(host, floatFromMantExp(mantissa, exponent, mode))
        .WillOnce(testing::Throw(std::runtime_error{"float from mant exp came apart"}));

    OutRegion out{32};
    EXPECT_EQ(
        hostContext.floatFromMantExp(mantissa, exponent, mode, out.slice()),
        hfErrorToInt(HostFunctionError::InternalFatal));
    EXPECT_THAT(logged(), testing::HasSubstr("float from mant exp came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("floatFromMantExp"));
}

// The out-region contract: write only if the whole value fits, and return the true length
// either way.
TEST_F(FloatFromMantExpCall, short_out_region_writes_nothing_and_returns_true_length)
{
    Bytes const result{1, 2, 3};
    EXPECT_CALL(host, floatFromMantExp(mantissa, exponent, mode)).WillOnce(testing::Return(result));

    OutRegion out{result.size() - 1};
    EXPECT_EQ(
        hostContext.floatFromMantExp(mantissa, exponent, mode, out.slice()),
        static_cast<std::int32_t>(result.size()));
    EXPECT_FALSE(out.wasWritten());
}

}  // namespace xrpl::test
