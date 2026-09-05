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
        makeHost()->floatAdd(slice(FloatTest::kOne), slice(FloatTest::kOne), -1),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatAddImpl, MalformedInput)
{
    expectError(
        makeHost()->floatAdd(slice(FloatTest::kOne), Slice{}, 0),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatAddImpl, MaxIouPlusMaxExpIsMaxExp)
{
    expectValue(
        makeHost()->floatAdd(slice(FloatTest::kMaxIOU), slice(FloatTest::kMaxExp), 0),
        FloatTest::kMaxExp);
}

TEST_F(FloatAddImpl, MinPlusZeroIsMin)
{
    expectValue(
        makeHost()->floatAdd(slice(FloatTest::kIntMin), slice(FloatTest::kIntZero), 0),
        FloatTest::kIntMin);
}

TEST_F(FloatAddImpl, MaxPlusMinIsZero)
{
    expectValue(
        makeHost()->floatAdd(slice(FloatTest::kIntMax), slice(FloatTest::kIntMin), 0),
        FloatTest::kIntZero);
}

}  // namespace xrpl::test
