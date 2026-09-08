#include <xrpl/basics/Number.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/FloatFixture.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct FloatPowerImpl : FloatTest
{
};

TEST_F(FloatPowerImpl, BadModeIsMalformed)
{
    expectError(
        makeHost()->floatPower(slice(FloatTest::kOne), 2, -1),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatPowerImpl, MalformedInput)
{
    expectError(makeHost()->floatPower(Slice{}, 3, 0), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatPowerImpl, NegativeDegreeIsMalformed)
{
    expectError(
        makeHost()->floatPower(slice(FloatTest::kOne), -2, 0),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatPowerImpl, OverflowIsComputationError)
{
    expectError(
        makeHost()->floatPower(slice(FloatTest::kMax), 2, 0),
        HostFunctionError::FloatComputationError);
}

TEST_F(FloatPowerImpl, DegreeTooLargeIsMalformed)
{
    expectError(
        makeHost()->floatPower(slice(FloatTest::kMax), Number::kMaxExponent + 1, 0),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatPowerImpl, DegreeZeroIsOne)
{
    expectValue(makeHost()->floatPower(slice(FloatTest::kMaxIOU), 0, 0), FloatTest::kOne);
}

TEST_F(FloatPowerImpl, DegreeOneIsIdentity)
{
    expectValue(makeHost()->floatPower(slice(FloatTest::kMaxIOU), 1, 0), FloatTest::kMaxIOU);
}

TEST_F(FloatPowerImpl, TenSquaredIsHundred)
{
    auto h = makeHost();
    auto const hundred = h->floatFromMantExp(100, 0, 0);
    ASSERT_TRUE(hundred.has_value());
    expectValue(h->floatPower(slice(FloatTest::kTen), 2, 0), *hundred);
}

TEST_F(FloatPowerImpl, TenthSquaredIsHundredth)
{
    auto h = makeHost();
    auto const tenth = h->floatFromMantExp(1, -1, 0);
    auto const hundredth = h->floatFromMantExp(1, -2, 0);
    ASSERT_TRUE(tenth.has_value() && hundredth.has_value());
    expectValue(h->floatPower(slice(*tenth), 2, 0), *hundredth);
}

}  // namespace xrpl::test
