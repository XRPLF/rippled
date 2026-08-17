#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/FloatFixture.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct FloatDivideImpl : FloatTest
{
};

TEST_F(FloatDivideImpl, BadModeIsMalformed)
{
    expectError(
        makeHost()->floatDivide(slice(floats::kOne), slice(floats::kOne), -1),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatDivideImpl, MalformedInput)
{
    expectError(
        makeHost()->floatDivide(slice(floats::kOne), Slice{}, 0),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatDivideImpl, DivideByZeroIsComputationError)
{
    expectError(
        makeHost()->floatDivide(slice(floats::kOne), slice(floats::kIntZero), 0),
        HostFunctionError::FloatComputationError);
}

TEST_F(FloatDivideImpl, OverflowIsComputationError)
{
    // A divisor just below 1, so max / it overflows.
    auto const y = makeHost()->floatFromMantExp(STAmount::kMaxValue, -floats::kNormalExp - 1, 0);
    ASSERT_TRUE(y.has_value());
    expectError(
        makeHost()->floatDivide(slice(floats::kMax), slice(*y), 0),
        HostFunctionError::FloatComputationError);
}

TEST_F(FloatDivideImpl, ZeroDividedByOneIsZero)
{
    expectValue(
        makeHost()->floatDivide(slice(floats::kIntZero), slice(floats::kOne), 0), floats::kIntZero);
}

TEST_F(FloatDivideImpl, MaxExpDividedByTenIsPreMaxExp)
{
    expectValue(
        makeHost()->floatDivide(slice(floats::kMaxExp), slice(floats::kTen), 0),
        floats::kPreMaxExp);
}

// The rounding mode changes an inexact result: 1/3 rounded Downward differs from Upward.
TEST_F(FloatDivideImpl, RoundingModeAffectsInexactResult)
{
    auto const three = makeHost()->floatFromInt(3, 0);
    ASSERT_TRUE(three.has_value());
    auto const down = makeHost()->floatDivide(slice(floats::kOne), slice(*three), 2);
    auto const up = makeHost()->floatDivide(slice(floats::kOne), slice(*three), 3);
    ASSERT_TRUE(down.has_value() && up.has_value());
    EXPECT_NE(*down, *up);
}

}  // namespace xrpl::test
