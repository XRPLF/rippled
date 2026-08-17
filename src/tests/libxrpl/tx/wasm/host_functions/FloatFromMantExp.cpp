#include <xrpl/basics/Number.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/FloatFixture.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct FloatFromMantExpImpl : FloatTest
{
    static constexpr int kMaxRawExp = Number::kMaxExponent + floats::kNormalExp;
    static constexpr int kMinRawExp = Number::kMinExponent + floats::kNormalExp;
};

TEST_F(FloatFromMantExpImpl, BadModeIsMalformed)
{
    expectError(makeHost()->floatFromMantExp(1, 0, -1), HostFunctionError::FloatInputMalformed);
    expectError(makeHost()->floatFromMantExp(1, 0, 4), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatFromMantExpImpl, ExponentTooHighIsMalformed)
{
    expectError(
        makeHost()->floatFromMantExp(1, kMaxRawExp + 1, 0), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatFromMantExpImpl, UnderflowIsZero)
{
    expectValue(makeHost()->floatFromMantExp(1, kMinRawExp - 1, 0), floats::kIntZero);
}

TEST_F(FloatFromMantExpImpl, MaxExponent)
{
    expectValue(makeHost()->floatFromMantExp(1, kMaxRawExp, 0), floats::kMaxExp);
}

TEST_F(FloatFromMantExpImpl, MinusMaxExponent)
{
    expectValue(makeHost()->floatFromMantExp(-1, kMaxRawExp, 0), floats::kMinusMaxExp);
}

TEST_F(FloatFromMantExpImpl, PreMaxExponent)
{
    expectValue(makeHost()->floatFromMantExp(1, kMaxRawExp - 1, 0), floats::kPreMaxExp);
}

TEST_F(FloatFromMantExpImpl, MaxIou)
{
    expectValue(
        makeHost()->floatFromMantExp(STAmount::kMaxValue, STAmount::kMaxOffset, 0),
        floats::kMaxIOU);
}

TEST_F(FloatFromMantExpImpl, MinExponent)
{
    expectValue(
        makeHost()->floatFromMantExp(1, Number::kMinExponent - floats::kNormalExp, 0),
        floats::kMinExp);
}

TEST_F(FloatFromMantExpImpl, TenTimesTenthIsOne)
{
    expectValue(makeHost()->floatFromMantExp(10, -1, 0), floats::kOne);
}

}  // namespace xrpl::test
