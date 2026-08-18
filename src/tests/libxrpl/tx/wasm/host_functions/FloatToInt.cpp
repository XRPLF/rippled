#include <xrpl/basics/Slice.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/FloatFixture.h>
#include <tx/wasm/RealHostFixture.h>

#include <cstdint>

namespace xrpl::test {

struct FloatToIntImpl : FloatTest
{
};

TEST_F(FloatToIntImpl, BadModeIsMalformed)
{
    expectError(
        makeHost()->floatToInt(slice(FloatTest::kOne), -1), HostFunctionError::FloatInputMalformed);
    expectError(
        makeHost()->floatToInt(slice(FloatTest::kOne), 4), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatToIntImpl, MalformedInputs)
{
    expectError(makeHost()->floatToInt(Slice{}, 0), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatToIntImpl, Zero)
{
    expectValue(makeHost()->floatToInt(slice(FloatTest::kIntZero), 0), std::int64_t{0});
}

TEST_F(FloatToIntImpl, One)
{
    expectValue(makeHost()->floatToInt(slice(FloatTest::kOne), 0), std::int64_t{1});
}

TEST_F(FloatToIntImpl, MinusOne)
{
    expectValue(makeHost()->floatToInt(slice(FloatTest::kMinusOne), 0), std::int64_t{-1});
}

TEST_F(FloatToIntImpl, Max)
{
    expectValue(makeHost()->floatToInt(slice(FloatTest::kIntMax), 0), kMax64);
}

TEST_F(FloatToIntImpl, Min)
{
    // floatIntMin rounds to -(2^63-1), i.e. -kMax64.
    expectValue(makeHost()->floatToInt(slice(FloatTest::kIntMin), 0), -kMax64);
}

TEST_F(FloatToIntImpl, OverflowsInt64IsComputationError)
{
    expectError(
        makeHost()->floatToInt(slice(FloatTest::kUintMax), 0),
        HostFunctionError::FloatComputationError);
}

TEST_F(FloatToIntImpl, PiRoundsByMode)
{
    expectValue(makeHost()->floatToInt(slice(FloatTest::kPi), 0), std::int64_t{3});  // ToNearest
    expectValue(makeHost()->floatToInt(slice(FloatTest::kPi), 1), std::int64_t{3});  // TowardsZero
    expectValue(makeHost()->floatToInt(slice(FloatTest::kPi), 2), std::int64_t{3});  // Downward
    expectValue(makeHost()->floatToInt(slice(FloatTest::kPi), 3), std::int64_t{4});  // Upward
}

}  // namespace xrpl::test
