#include <xrpl/basics/Slice.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/FloatFixture.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct FloatSubtractImpl : FloatTest
{
};

TEST_F(FloatSubtractImpl, BadModeIsMalformed)
{
    expectError(
        makeHost()->floatSubtract(slice(FloatTest::kOne), slice(FloatTest::kOne), -1),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatSubtractImpl, MalformedInput)
{
    expectError(
        makeHost()->floatSubtract(slice(FloatTest::kOne), Slice{}, 0),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatSubtractImpl, MinusMaxExpMinusMaxIouIsMinusMaxExp)
{
    expectValue(
        makeHost()->floatSubtract(slice(FloatTest::kMinusMaxExp), slice(FloatTest::kMaxIOU), 0),
        FloatTest::kMinusMaxExp);
}

TEST_F(FloatSubtractImpl, MinMinusZeroIsMin)
{
    expectValue(
        makeHost()->floatSubtract(slice(FloatTest::kIntMin), slice(FloatTest::kIntZero), 0),
        FloatTest::kIntMin);
}

TEST_F(FloatSubtractImpl, ZeroMinusOneIsMinusOne)
{
    expectValue(
        makeHost()->floatSubtract(slice(FloatTest::kIntZero), slice(FloatTest::kOne), 0),
        FloatTest::kMinusOne);
}

}  // namespace xrpl::test
