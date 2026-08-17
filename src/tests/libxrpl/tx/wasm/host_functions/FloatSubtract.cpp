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
        makeHost()->floatSubtract(slice(floats::kOne), slice(floats::kOne), -1),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatSubtractImpl, MalformedInput)
{
    expectError(
        makeHost()->floatSubtract(slice(floats::kOne), Slice{}, 0),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatSubtractImpl, MinusMaxExpMinusMaxIouIsMinusMaxExp)
{
    expectValue(
        makeHost()->floatSubtract(slice(floats::kMinusMaxExp), slice(floats::kMaxIOU), 0),
        floats::kMinusMaxExp);
}

TEST_F(FloatSubtractImpl, MinMinusZeroIsMin)
{
    expectValue(
        makeHost()->floatSubtract(slice(floats::kIntMin), slice(floats::kIntZero), 0),
        floats::kIntMin);
}

TEST_F(FloatSubtractImpl, ZeroMinusOneIsMinusOne)
{
    expectValue(
        makeHost()->floatSubtract(slice(floats::kIntZero), slice(floats::kOne), 0),
        floats::kMinusOne);
}

}  // namespace xrpl::test
