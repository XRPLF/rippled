#include <xrpl/basics/Number.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/FloatFixture.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct FloatFromMantExpImpl : FloatTest
{
    static constexpr int kMaxRawExp = Number::kMaxExponent + FloatTest::kNormalExp;
    static constexpr int kMinRawExp = Number::kMinExponent + FloatTest::kNormalExp;
};

TEST_F(FloatFromMantExpImpl, BadModeIsMalformed)
{
    auto h = makeHost();
    expectError(h->floatFromMantExp(1, 0, -1), HostFunctionError::FloatInputMalformed);
    expectError(h->floatFromMantExp(1, 0, 4), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatFromMantExpImpl, ExponentTooHighIsMalformed)
{
    expectError(
        makeHost()->floatFromMantExp(1, kMaxRawExp + 1, 0), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatFromMantExpImpl, UnderflowIsZero)
{
    expectValue(makeHost()->floatFromMantExp(1, kMinRawExp - 1, 0), FloatTest::kIntZero);
}

TEST_F(FloatFromMantExpImpl, MaxExponent)
{
    expectValue(makeHost()->floatFromMantExp(1, kMaxRawExp, 0), FloatTest::kMaxExp);
}

TEST_F(FloatFromMantExpImpl, MinusMaxExponent)
{
    expectValue(makeHost()->floatFromMantExp(-1, kMaxRawExp, 0), FloatTest::kMinusMaxExp);
}

TEST_F(FloatFromMantExpImpl, PreMaxExponent)
{
    expectValue(makeHost()->floatFromMantExp(1, kMaxRawExp - 1, 0), FloatTest::kPreMaxExp);
}

TEST_F(FloatFromMantExpImpl, MaxIou)
{
    expectValue(
        makeHost()->floatFromMantExp(STAmount::kMaxValue, STAmount::kMaxOffset, 0),
        FloatTest::kMaxIOU);
}

TEST_F(FloatFromMantExpImpl, MinExponent)
{
    expectValue(
        makeHost()->floatFromMantExp(1, Number::kMinExponent - FloatTest::kNormalExp, 0),
        FloatTest::kMinExp);
}

TEST_F(FloatFromMantExpImpl, TenTimesTenthIsOne)
{
    expectValue(makeHost()->floatFromMantExp(10, -1, 0), FloatTest::kOne);
}

}  // namespace xrpl::test
