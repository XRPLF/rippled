#include <xrpl/basics/Slice.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/FloatFixture.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct FloatMultiplyImpl : FloatTest
{
};

TEST_F(FloatMultiplyImpl, bad_mode_is_malformed)
{
    expectError(
        makeHost()->floatMultiply(slice(FloatTest::kOne), slice(FloatTest::kOne), -1),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatMultiplyImpl, malformed_input)
{
    expectError(
        makeHost()->floatMultiply(slice(FloatTest::kOne), Slice{}, 0),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatMultiplyImpl, overflow_is_computation_error)
{
    expectError(
        makeHost()->floatMultiply(slice(FloatTest::kMax), slice(FloatTest::kOneMore), 0),
        HostFunctionError::FloatComputationError);
}

TEST_F(FloatMultiplyImpl, one_times_one_is_one)
{
    expectValue(
        makeHost()->floatMultiply(slice(FloatTest::kOne), slice(FloatTest::kOne), 0),
        FloatTest::kOne);
}

TEST_F(FloatMultiplyImpl, zero_times_max_iou_is_zero)
{
    expectValue(
        makeHost()->floatMultiply(slice(FloatTest::kIntZero), slice(FloatTest::kMaxIOU), 0),
        FloatTest::kIntZero);
}

TEST_F(FloatMultiplyImpl, ten_times_pre_max_exp_is_max_exp)
{
    expectValue(
        makeHost()->floatMultiply(slice(FloatTest::kTen), slice(FloatTest::kPreMaxExp), 0),
        FloatTest::kMaxExp);
}

}  // namespace xrpl::test
