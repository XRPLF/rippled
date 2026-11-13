#include <xrpl/protocol/Rules.h>

#include <xrpl/basics/Number.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/UintTypes.h>

#include <gtest/gtest.h>

#include <unordered_set>

using namespace xrpl;

TEST(CurrentTransactionRulesGuard, restores_mantissa_scale)
{
    NumberMantissaScaleGuard const outer(MantissaRange::MantissaScale::Small);
    ASSERT_EQ(Number::getMantissaScale(), MantissaRange::MantissaScale::Small);

    {
        // Rules with MPTokensV2 enabled select a large mantissa scale.
        CurrentTransactionRulesGuard const guard(
            Rules{std::unordered_set<UInt256, beast::Uhash<>>{featureMPTokensV2}});
        EXPECT_NE(Number::getMantissaScale(), MantissaRange::MantissaScale::Small);
    }

    // The guard restores the scale that was active before it, not the scale implied by the
    // (empty) saved rules.
    EXPECT_EQ(Number::getMantissaScale(), MantissaRange::MantissaScale::Small);
}
