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

TEST_F(FloatFromMantExpImpl, bad_mode_is_malformed)
{
    auto h = makeHost();
    expectError(h->floatFromMantExp(1, 0, -1), HostFunctionError::FloatInputMalformed);
    expectError(h->floatFromMantExp(1, 0, 4), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatFromMantExpImpl, exponent_too_high_is_malformed)
{
    expectError(
        makeHost()->floatFromMantExp(1, kMaxRawExp + 1, 0), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatFromMantExpImpl, underflow_is_zero)
{
    expectValue(makeHost()->floatFromMantExp(1, kMinRawExp - 1, 0), FloatTest::kIntZero);
}

TEST_F(FloatFromMantExpImpl, max_exponent)
{
    expectValue(makeHost()->floatFromMantExp(1, kMaxRawExp, 0), FloatTest::kMaxExp);
}

TEST_F(FloatFromMantExpImpl, minus_max_exponent)
{
    expectValue(makeHost()->floatFromMantExp(-1, kMaxRawExp, 0), FloatTest::kMinusMaxExp);
}

TEST_F(FloatFromMantExpImpl, pre_max_exponent)
{
    expectValue(makeHost()->floatFromMantExp(1, kMaxRawExp - 1, 0), FloatTest::kPreMaxExp);
}

TEST_F(FloatFromMantExpImpl, max_iou)
{
    expectValue(
        makeHost()->floatFromMantExp(STAmount::kMaxValue, STAmount::kMaxOffset, 0),
        FloatTest::kMaxIOU);
}

TEST_F(FloatFromMantExpImpl, min_exponent)
{
    expectValue(
        makeHost()->floatFromMantExp(1, Number::kMinExponent - FloatTest::kNormalExp, 0),
        FloatTest::kMinExp);
}

TEST_F(FloatFromMantExpImpl, ten_times_tenth_is_one)
{
    expectValue(makeHost()->floatFromMantExp(10, -1, 0), FloatTest::kOne);
}

}  // namespace xrpl::test
