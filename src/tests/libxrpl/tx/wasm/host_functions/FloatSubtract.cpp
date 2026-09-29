#include <xrpl/basics/Slice.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/FloatFixture.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct FloatSubtractImpl : FloatTest
{
};

TEST_F(FloatSubtractImpl, bad_mode_is_malformed)
{
    expectError(
        makeHost()->floatSubtract(slice(FloatTest::kOne), slice(FloatTest::kOne), -1),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatSubtractImpl, malformed_input)
{
    expectError(
        makeHost()->floatSubtract(slice(FloatTest::kOne), Slice{}, 0),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatSubtractImpl, minus_max_exp_minus_max_iou_is_minus_max_exp)
{
    expectValue(
        makeHost()->floatSubtract(slice(FloatTest::kMinusMaxExp), slice(FloatTest::kMaxIOU), 0),
        FloatTest::kMinusMaxExp);
}

TEST_F(FloatSubtractImpl, min_minus_zero_is_min)
{
    expectValue(
        makeHost()->floatSubtract(slice(FloatTest::kIntMin), slice(FloatTest::kIntZero), 0),
        FloatTest::kIntMin);
}

TEST_F(FloatSubtractImpl, zero_minus_one_is_minus_one)
{
    expectValue(
        makeHost()->floatSubtract(slice(FloatTest::kIntZero), slice(FloatTest::kOne), 0),
        FloatTest::kMinusOne);
}

}  // namespace xrpl::test
