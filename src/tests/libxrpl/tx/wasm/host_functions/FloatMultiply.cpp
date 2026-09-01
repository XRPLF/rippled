#include <xrpl/basics/Slice.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/FloatFixture.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct FloatMultiplyImpl : FloatTest
{
};

TEST_F(FloatMultiplyImpl, BadModeIsMalformed)
{
    expectError(
        makeHost()->floatMultiply(slice(FloatTest::kOne), slice(FloatTest::kOne), -1),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatMultiplyImpl, MalformedInput)
{
    expectError(
        makeHost()->floatMultiply(slice(FloatTest::kOne), Slice{}, 0),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatMultiplyImpl, OverflowIsComputationError)
{
    expectError(
        makeHost()->floatMultiply(slice(FloatTest::kMax), slice(FloatTest::kOneMore), 0),
        HostFunctionError::FloatComputationError);
}

TEST_F(FloatMultiplyImpl, OneTimesOneIsOne)
{
    expectValue(
        makeHost()->floatMultiply(slice(FloatTest::kOne), slice(FloatTest::kOne), 0),
        FloatTest::kOne);
}

TEST_F(FloatMultiplyImpl, ZeroTimesMaxIouIsZero)
{
    expectValue(
        makeHost()->floatMultiply(slice(FloatTest::kIntZero), slice(FloatTest::kMaxIOU), 0),
        FloatTest::kIntZero);
}

TEST_F(FloatMultiplyImpl, TenTimesPreMaxExpIsMaxExp)
{
    expectValue(
        makeHost()->floatMultiply(slice(FloatTest::kTen), slice(FloatTest::kPreMaxExp), 0),
        FloatTest::kMaxExp);
}

}  // namespace xrpl::test
