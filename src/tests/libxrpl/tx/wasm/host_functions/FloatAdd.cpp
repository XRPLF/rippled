#include <xrpl/basics/Slice.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/FloatFixture.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct FloatAddImpl : FloatTest
{
};

TEST_F(FloatAddImpl, bad_mode_is_malformed)
{
    expectError(
        makeHost()->floatAdd(slice(FloatTest::kOne), slice(FloatTest::kOne), -1),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatAddImpl, malformed_input)
{
    expectError(
        makeHost()->floatAdd(slice(FloatTest::kOne), Slice{}, 0),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatAddImpl, max_iou_plus_max_exp_is_max_exp)
{
    expectValue(
        makeHost()->floatAdd(slice(FloatTest::kMaxIOU), slice(FloatTest::kMaxExp), 0),
        FloatTest::kMaxExp);
}

TEST_F(FloatAddImpl, min_plus_zero_is_min)
{
    expectValue(
        makeHost()->floatAdd(slice(FloatTest::kIntMin), slice(FloatTest::kIntZero), 0),
        FloatTest::kIntMin);
}

TEST_F(FloatAddImpl, max_plus_min_is_zero)
{
    expectValue(
        makeHost()->floatAdd(slice(FloatTest::kIntMax), slice(FloatTest::kIntMin), 0),
        FloatTest::kIntZero);
}

}  // namespace xrpl::test
