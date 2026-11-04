#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/FloatFixture.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct FloatDivideImpl : FloatTest
{
};

TEST_F(FloatDivideImpl, bad_mode_is_malformed)
{
    expectError(
        makeHost()->floatDivide(slice(FloatTest::kOne), slice(FloatTest::kOne), -1),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatDivideImpl, malformed_input)
{
    expectError(
        makeHost()->floatDivide(slice(FloatTest::kOne), Slice{}, 0),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatDivideImpl, divide_by_zero_is_computation_error)
{
    expectError(
        makeHost()->floatDivide(slice(FloatTest::kOne), slice(FloatTest::kIntZero), 0),
        HostFunctionError::FloatComputationError);
}

TEST_F(FloatDivideImpl, overflow_is_computation_error)
{
    // A divisor just below 1, so max / it overflows.
    auto h = makeHost();
    auto const y = h->floatFromMantExp(STAmount::kMaxValue, -FloatTest::kNormalExp - 1, 0);
    ASSERT_TRUE(y.has_value());
    expectError(
        h->floatDivide(slice(FloatTest::kMax), slice(*y), 0),
        HostFunctionError::FloatComputationError);
}

TEST_F(FloatDivideImpl, zero_divided_by_one_is_zero)
{
    expectValue(
        makeHost()->floatDivide(slice(FloatTest::kIntZero), slice(FloatTest::kOne), 0),
        FloatTest::kIntZero);
}

TEST_F(FloatDivideImpl, max_exp_divided_by_ten_is_pre_max_exp)
{
    expectValue(
        makeHost()->floatDivide(slice(FloatTest::kMaxExp), slice(FloatTest::kTen), 0),
        FloatTest::kPreMaxExp);
}

// The rounding mode changes an inexact result: 1/3 rounded Downward differs from Upward.
TEST_F(FloatDivideImpl, rounding_mode_affects_inexact_result)
{
    auto h = makeHost();
    auto const three = h->floatFromInt(3, 0);
    ASSERT_TRUE(three.has_value());
    auto const down = h->floatDivide(slice(FloatTest::kOne), slice(*three), 2);
    auto const up = h->floatDivide(slice(FloatTest::kOne), slice(*three), 3);
    ASSERT_TRUE(down.has_value() && up.has_value());
    EXPECT_NE(*down, *up);
}

}  // namespace xrpl::test
