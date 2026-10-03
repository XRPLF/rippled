#include <xrpl/protocol/MPTAmount.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Rules.h>

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace xrpl {

namespace {

constexpr auto kMaxMpt = std::numeric_limits<MPTAmount::value_type>::max();
constexpr auto kMinMpt = std::numeric_limits<MPTAmount::value_type>::min();

Rules
makeRules(bool withFix)
{
    // Rules keeps a reference to its presets, so they must outlive it.
    static std::unordered_set<uint256, beast::Uhash<>> const kWithFix{featureMPTokensV2};
    static std::unordered_set<uint256, beast::Uhash<>> const kWithoutFix;
    return Rules{withFix ? kWithFix : kWithoutFix};
}

void
expectBoundariesDoNotThrow()
{
    MPTAmount const max(kMaxMpt);
    MPTAmount const min(kMinMpt);

    EXPECT_EQ(MPTAmount(kMaxMpt - 1) + MPTAmount(1), max);
    EXPECT_EQ(MPTAmount(kMinMpt + 1) - MPTAmount(1), min);
    EXPECT_EQ(MPTAmount(kMinMpt + 1) + MPTAmount(-1), min);
    EXPECT_EQ(MPTAmount(kMaxMpt - 1) - MPTAmount(-1), max);
    EXPECT_EQ(max + min, MPTAmount(-1));
    EXPECT_EQ(max - max, MPTAmount(0));
    EXPECT_EQ(min - min, MPTAmount(0));
    EXPECT_EQ(MPTAmount(kMaxMpt / 2) + MPTAmount(kMaxMpt / 2), MPTAmount(kMaxMpt - 1));
}

void
expectOverflowsThrow()
{
    // += and -= leave the lhs unchanged after the throw
    {
        MPTAmount a(kMaxMpt);
        EXPECT_THROW(a += MPTAmount(1), std::overflow_error);
        EXPECT_EQ(a, MPTAmount(kMaxMpt));

        MPTAmount b(kMinMpt);
        EXPECT_THROW(b += MPTAmount(-1), std::overflow_error);
        EXPECT_EQ(b, MPTAmount(kMinMpt));

        MPTAmount c(kMinMpt);
        EXPECT_THROW(c -= MPTAmount(1), std::overflow_error);
        EXPECT_EQ(c, MPTAmount(kMinMpt));

        MPTAmount d(kMaxMpt);
        EXPECT_THROW(d -= MPTAmount(-1), std::overflow_error);
        EXPECT_EQ(d, MPTAmount(kMaxMpt));
    }

    // Binary + and - are built from the compound operators
    {
        // Two amounts that are each a legal MPT balance
        MPTAmount const half((kMaxMpt / 2) + 1);
        EXPECT_THROW((void)(half + half), std::overflow_error);

        MPTAmount const max(kMaxMpt);
        MPTAmount const negMax = MPTAmount(0) - max;
        EXPECT_THROW((void)(negMax - max), std::overflow_error);
    }
}

}  // namespace

TEST(MPTAmountTest, overflow_throws_with_fix)
{
    CurrentTransactionRulesGuard const rg(makeRules(true));
    expectOverflowsThrow();
    expectBoundariesDoNotThrow();
}

// MPT overflow implies an MPTokensV2 path, so with no rules set the
// operators throw as if the amendment were enabled.
TEST(MPTAmountTest, overflow_throws_without_rules)
{
    ASSERT_FALSE(getCurrentTransactionRules());
    expectOverflowsThrow();
    expectBoundariesDoNotThrow();
}

// Without the amendment an overflow keeps the legacy signed arithmetic, which
// is undefined behavior, so only results that do not overflow are checked.
TEST(MPTAmountTest, boundaries_without_fix)
{
    CurrentTransactionRulesGuard const rg(makeRules(false));
    expectBoundariesDoNotThrow();
}

}  // namespace xrpl
