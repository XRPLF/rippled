#include <xrpl/protocol/STAmount.h>

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>

using namespace xrpl;

namespace {

// A 25% transfer fee.
constexpr std::uint32_t kRate125 = 1'250'000'000;

AccountID const kIssuer{1};

}  // namespace

TEST(STAmount, mul_ratio_iou)
{
    Issue const usd{toCurrency("USD"), kIssuer};

    EXPECT_EQ(mulRatio(STAmount{usd, 100}, QUALITY_ONE, kRate125, false), STAmount(usd, 80));
    EXPECT_EQ(mulRatio(STAmount{usd, 80}, kRate125, QUALITY_ONE, true), STAmount(usd, 100));

    // 1 / 1.7 does not fit in a 16-digit mantissa, so the rounding direction
    // decides the last digit.
    STAmount const one{usd, 1};
    EXPECT_LT(
        mulRatio(one, QUALITY_ONE, 1'700'000'000, false),
        mulRatio(one, QUALITY_ONE, 1'700'000'000, true));

    STAmount const full{usd, std::uint64_t{9'999'999'999'999'999}, -5};
    EXPECT_EQ(mulRatio(full, QUALITY_ONE, QUALITY_ONE, false), full);
}

TEST(STAmount, mul_ratio_mpt)
{
    MPTIssue const mpt{makeMptID(1, kIssuer)};

    EXPECT_EQ(mulRatio(STAmount{mpt, 31}, QUALITY_ONE, kRate125, false), STAmount(mpt, 24));
    EXPECT_EQ(mulRatio(STAmount{mpt, 31}, QUALITY_ONE, kRate125, true), STAmount(mpt, 25));
    EXPECT_EQ(mulRatio(STAmount{mpt, 1}, QUALITY_ONE, kRate125, false), STAmount(mpt));
    EXPECT_EQ(mulRatio(STAmount{mpt, 1}, QUALITY_ONE, kRate125, true), STAmount(mpt, 1));

    STAmount const max{mpt, kMaxMpTokenAmount};
    EXPECT_EQ(mulRatio(max, QUALITY_ONE, QUALITY_ONE, false), max);
    EXPECT_NO_THROW((void)mulRatio(max, QUALITY_ONE, kRate125, false));
    EXPECT_THROW((void)mulRatio(max, kRate125, QUALITY_ONE, true), std::overflow_error);
}

TEST(STAmount, mul_ratio_xrp)
{
    EXPECT_EQ(
        mulRatio(STAmount{XRPAmount{31}}, QUALITY_ONE, kRate125, false), STAmount{XRPAmount{24}});
    EXPECT_EQ(
        mulRatio(STAmount{XRPAmount{31}}, QUALITY_ONE, kRate125, true), STAmount{XRPAmount{25}});
}
