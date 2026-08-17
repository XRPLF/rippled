#include <xrpl/basics/Slice.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <tx/wasm/FloatFixture.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct FloatRootImpl : FloatTest
{
};

TEST_F(FloatRootImpl, BadModeIsMalformed)
{
    expectError(
        makeHost()->floatRoot(slice(floats::kOne), 2, -1), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatRootImpl, MalformedInput)
{
    expectError(makeHost()->floatRoot(Slice{}, 3, 0), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatRootImpl, NegativeDegreeIsMalformed)
{
    expectError(
        makeHost()->floatRoot(slice(floats::kOne), -2, 0), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatRootImpl, RootOfZeroIsZero)
{
    expectValue(makeHost()->floatRoot(slice(floats::kIntZero), 2, 0), floats::kIntZero);
}

TEST_F(FloatRootImpl, FirstRootIsIdentity)
{
    expectValue(makeHost()->floatRoot(slice(floats::kMaxIOU), 1, 0), floats::kMaxIOU);
}

TEST_F(FloatRootImpl, SquareRootOfHundredIsTen)
{
    auto const hundred = makeHost()->floatFromMantExp(100, 0, 0);
    ASSERT_TRUE(hundred.has_value());
    expectValue(makeHost()->floatRoot(slice(*hundred), 2, 0), floats::kTen);
}

TEST_F(FloatRootImpl, CubeRootOfThousandIsTen)
{
    auto const thousand = makeHost()->floatFromMantExp(1000, 0, 0);
    ASSERT_TRUE(thousand.has_value());
    expectValue(makeHost()->floatRoot(slice(*thousand), 3, 0), floats::kTen);
}

TEST_F(FloatRootImpl, SquareRootOfHundredthIsTenth)
{
    auto const hundredth = makeHost()->floatFromMantExp(1, -2, 0);
    auto const tenth = makeHost()->floatFromMantExp(1, -1, 0);
    ASSERT_TRUE(hundredth.has_value() && tenth.has_value());
    expectValue(makeHost()->floatRoot(slice(*hundredth), 2, 0), *tenth);
}

}  // namespace xrpl::test
