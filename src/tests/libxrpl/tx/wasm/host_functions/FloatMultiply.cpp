#include <xrpl/basics/Slice.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/FloatFixture.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct FloatMultiplyImpl : FloatTest
{
};

TEST_F(FloatMultiplyImpl, BadModeIsMalformed)
{
    expectError(
        makeHost()->floatMultiply(slice(floats::kOne), slice(floats::kOne), -1),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatMultiplyImpl, MalformedInput)
{
    expectError(
        makeHost()->floatMultiply(slice(floats::kOne), Slice{}, 0),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatMultiplyImpl, OverflowIsComputationError)
{
    expectError(
        makeHost()->floatMultiply(slice(floats::kMax), slice(floats::kOneMore), 0),
        HostFunctionError::FloatComputationError);
}

TEST_F(FloatMultiplyImpl, OneTimesOneIsOne)
{
    expectValue(
        makeHost()->floatMultiply(slice(floats::kOne), slice(floats::kOne), 0), floats::kOne);
}

TEST_F(FloatMultiplyImpl, ZeroTimesMaxIouIsZero)
{
    expectValue(
        makeHost()->floatMultiply(slice(floats::kIntZero), slice(floats::kMaxIOU), 0),
        floats::kIntZero);
}

TEST_F(FloatMultiplyImpl, TenTimesPreMaxExpIsMaxExp)
{
    expectValue(
        makeHost()->floatMultiply(slice(floats::kTen), slice(floats::kPreMaxExp), 0),
        floats::kMaxExp);
}

}  // namespace xrpl::test
