#include <xrpl/basics/Slice.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/FloatFixture.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct FloatAddImpl : FloatTest
{
};

TEST_F(FloatAddImpl, BadModeIsMalformed)
{
    expectError(
        makeHost()->floatAdd(slice(floats::kOne), slice(floats::kOne), -1),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatAddImpl, MalformedInput)
{
    expectError(
        makeHost()->floatAdd(slice(floats::kOne), Slice{}, 0),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatAddImpl, MaxIouPlusMaxExpIsMaxExp)
{
    expectValue(
        makeHost()->floatAdd(slice(floats::kMaxIOU), slice(floats::kMaxExp), 0), floats::kMaxExp);
}

TEST_F(FloatAddImpl, MinPlusZeroIsMin)
{
    expectValue(
        makeHost()->floatAdd(slice(floats::kIntMin), slice(floats::kIntZero), 0), floats::kIntMin);
}

TEST_F(FloatAddImpl, MaxPlusMinIsZero)
{
    expectValue(
        makeHost()->floatAdd(slice(floats::kIntMax), slice(floats::kIntMin), 0), floats::kIntZero);
}

}  // namespace xrpl::test
