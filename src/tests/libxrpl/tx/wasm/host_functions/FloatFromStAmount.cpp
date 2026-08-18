#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/FloatFixture.h>
#include <tx/wasm/RealHostFixture.h>

#include <cstdint>

namespace xrpl::test {

struct FloatFromStAmountImpl : FloatTest
{
    static Issue
    usd()
    {
        return Issue{toCurrency("USD"), Account{"gw"}.id()};
    }
};

TEST_F(FloatFromStAmountImpl, BadModeIsMalformed)
{
    auto const amount = STAmount{XRP(100)};
    expectError(makeHost()->floatFromSTAmount(amount, -1), HostFunctionError::FloatInputMalformed);
    expectError(makeHost()->floatFromSTAmount(amount, 4), HostFunctionError::FloatInputMalformed);
}

TEST_F(FloatFromStAmountImpl, ZeroXrp)
{
    expectValue(makeHost()->floatFromSTAmount(STAmount{XRP(0)}, 0), FloatTest::kIntZero);
}

TEST_F(FloatFromStAmountImpl, MinusOneXrp)
{
    // -1 XRP == -1'000'000 drops.
    auto const expected = makeHost()->floatFromMantExp(-1'000'000, 0, 0);
    ASSERT_TRUE(expected.has_value());
    expectValue(makeHost()->floatFromSTAmount(STAmount{XRP(-1)}, 0), *expected);
}

TEST_F(FloatFromStAmountImpl, MaxDrops)
{
    auto const expected = makeHost()->floatFromMantExp(9'223'372'036'854'776, 3, 0);
    ASSERT_TRUE(expected.has_value());
    expectValue(makeHost()->floatFromSTAmount(STAmount{noIssue(), kMax64}, 0), *expected);
}

TEST_F(FloatFromStAmountImpl, MinIou)
{
    auto const amount = STAmount{
        IOUAmount{static_cast<std::int64_t>(STAmount::kMinValue), STAmount::kMinOffset}, usd()};
    expectValue(makeHost()->floatFromSTAmount(amount, 0), FloatTest::kMinIOU);
}

TEST_F(FloatFromStAmountImpl, MaxIou)
{
    auto const amount = STAmount{
        IOUAmount{static_cast<std::int64_t>(STAmount::kMaxValue), STAmount::kMaxOffset}, usd()};
    expectValue(makeHost()->floatFromSTAmount(amount, 0), FloatTest::kMaxIOU);
}

}  // namespace xrpl::test
