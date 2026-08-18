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
        makeHost()->floatRoot(slice(FloatTest::kOne), 2, -1),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatRootImpl, MalformedInput)
{
    expectError(makeHost()->floatRoot(Slice{}, 3, 0), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatRootImpl, NegativeDegreeIsMalformed)
{
    expectError(
        makeHost()->floatRoot(slice(FloatTest::kOne), -2, 0),
        HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatRootImpl, RootOfZeroIsZero)
{
    expectValue(makeHost()->floatRoot(slice(FloatTest::kIntZero), 2, 0), FloatTest::kIntZero);
}

TEST_F(FloatRootImpl, FirstRootIsIdentity)
{
    expectValue(makeHost()->floatRoot(slice(FloatTest::kMaxIOU), 1, 0), FloatTest::kMaxIOU);
}

TEST_F(FloatRootImpl, SquareRootOfHundredIsTen)
{
    auto h = makeHost();
    auto const hundred = h->floatFromMantExp(100, 0, 0);
    ASSERT_TRUE(hundred.has_value());
    expectValue(h->floatRoot(slice(*hundred), 2, 0), FloatTest::kTen);
}

TEST_F(FloatRootImpl, CubeRootOfThousandIsTen)
{
    auto h = makeHost();
    auto const thousand = h->floatFromMantExp(1000, 0, 0);
    ASSERT_TRUE(thousand.has_value());
    expectValue(h->floatRoot(slice(*thousand), 3, 0), FloatTest::kTen);
}

TEST_F(FloatRootImpl, SquareRootOfHundredthIsTenth)
{
    auto h = makeHost();
    auto const hundredth = h->floatFromMantExp(1, -2, 0);
    auto const tenth = h->floatFromMantExp(1, -1, 0);
    ASSERT_TRUE(hundredth.has_value() && tenth.has_value());
    expectValue(h->floatRoot(slice(*hundredth), 2, 0), *tenth);
}

}  // namespace xrpl::test
