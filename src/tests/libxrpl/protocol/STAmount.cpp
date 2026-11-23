#include <xrpl/protocol/STAmount.h>

#include <xrpl/basics/Number.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
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
#include <utility>

using namespace xrpl;

namespace {

// A 25% transfer fee.
constexpr std::uint32_t kTransferRate125 = 1'250'000'000;

AccountID const kUsdIssuer{1};

}  // namespace

TEST(STAmount, mul_ratio_iou)
{
    Issue const usd{toCurrency("USD"), kUsdIssuer};

    EXPECT_EQ(
        mulRatio(STAmount{usd, 100}, QUALITY_ONE, kTransferRate125, Number::RoundingMode::Downward),
        STAmount(usd, 80));
    EXPECT_EQ(
        mulRatio(STAmount{usd, 80}, kTransferRate125, QUALITY_ONE, Number::RoundingMode::Upward),
        STAmount(usd, 100));

    // 1 / 1.7 does not fit in a 16-digit mantissa, so the rounding direction
    // decides the last digit.
    STAmount const one{usd, 1};
    EXPECT_LT(
        mulRatio(one, QUALITY_ONE, 1'700'000'000, Number::RoundingMode::Downward),
        mulRatio(one, QUALITY_ONE, 1'700'000'000, Number::RoundingMode::Upward));

    STAmount const full{usd, std::uint64_t{9'999'999'999'999'999}, -5};
    EXPECT_EQ(mulRatio(full, QUALITY_ONE, QUALITY_ONE, Number::RoundingMode::Downward), full);
}

TEST(STAmount, mul_ratio_mpt)
{
    MPTIssue const mpt{makeMptID(1, kUsdIssuer)};

    EXPECT_EQ(
        mulRatio(STAmount{mpt, 31}, QUALITY_ONE, kTransferRate125, Number::RoundingMode::Downward),
        STAmount(mpt, 24));
    EXPECT_EQ(
        mulRatio(STAmount{mpt, 31}, QUALITY_ONE, kTransferRate125, Number::RoundingMode::Upward),
        STAmount(mpt, 25));
    EXPECT_EQ(
        mulRatio(STAmount{mpt, 1}, QUALITY_ONE, kTransferRate125, Number::RoundingMode::Downward),
        STAmount(mpt));
    EXPECT_EQ(
        mulRatio(STAmount{mpt, 1}, QUALITY_ONE, kTransferRate125, Number::RoundingMode::Upward),
        STAmount(mpt, 1));

    STAmount const max{mpt, kMaxMpTokenAmount};
    EXPECT_EQ(mulRatio(max, QUALITY_ONE, QUALITY_ONE, Number::RoundingMode::Downward), max);
    EXPECT_NO_THROW(
        (void)mulRatio(max, QUALITY_ONE, kTransferRate125, Number::RoundingMode::Downward));
    EXPECT_THROW(
        (void)mulRatio(max, kTransferRate125, QUALITY_ONE, Number::RoundingMode::Upward),
        std::overflow_error);
}

TEST(STAmount, mul_ratio_xrp)
{
    EXPECT_EQ(
        mulRatio(
            STAmount{XRPAmount{31}}, QUALITY_ONE, kTransferRate125, Number::RoundingMode::Downward),
        STAmount{XRPAmount{24}});
    EXPECT_EQ(
        mulRatio(
            STAmount{XRPAmount{31}}, QUALITY_ONE, kTransferRate125, Number::RoundingMode::Upward),
        STAmount{XRPAmount{25}});
}

namespace {

struct TestAssets
{
    Issue usd{toCurrency("USD"), kUsdIssuer};
    MPTIssue mpt{makeMptID(1, kUsdIssuer)};
    Asset iou{usd};
    Asset mptAsset{mpt};
    Asset xrp{xrpIssue()};
};

constexpr auto kDown = Number::RoundingMode::Downward;
constexpr auto kUp = Number::RoundingMode::Upward;

}  // namespace

TEST(STAmount, mul_div_round_up_and_down)
{
    TestAssets const assets;

    // a * b / c, with its floor and ceiling.
    struct Row
    {
        std::int64_t a = 0;
        std::int64_t b = 0;
        std::int64_t c = 1;
        std::int64_t floor = 0;
        std::int64_t ceil = 0;
    };
    Row const rows[] = {
        {.a = 7, .b = 1, .c = 2, .floor = 3, .ceil = 4},
        {.a = 10, .b = 1, .c = 3, .floor = 3, .ceil = 4},
        {.a = 11, .b = 1, .c = 3, .floor = 3, .ceil = 4},
        {.a = 6, .b = 1, .c = 3, .floor = 2, .ceil = 2},
        {.a = 1, .b = 1, .c = 1000, .floor = 0, .ceil = 1},
        {.a = 12, .b = 12, .c = 5, .floor = 28, .ceil = 29},
    };
    for (auto const& row : rows)
    {
        for (Asset const& asset : {assets.iou, assets.mptAsset, assets.xrp})
        {
            // Whole units: the integral assets always, an IOU through targetExponent 0.
            auto const down =
                mulDivRound(Number{row.a}, Number{row.b}, Number{row.c}, asset, kDown, 0);
            auto const up = mulDivRound(Number{row.a}, Number{row.b}, Number{row.c}, asset, kUp, 0);
            EXPECT_EQ(Number{down}, Number{row.floor}) << row.a << " * " << row.b << " / " << row.c;
            EXPECT_EQ(Number{up}, Number{row.ceil}) << row.a << " * " << row.b << " / " << row.c;
            EXPECT_TRUE(down.asset() == asset);
            EXPECT_TRUE(up.asset() == asset);
            // An integral asset needs no target.
            if (asset.integral())
            {
                EXPECT_EQ(
                    mulDivRound(Number{row.a}, Number{row.b}, Number{row.c}, asset, kDown), down);
                EXPECT_EQ(mulDivRound(Number{row.a}, Number{row.b}, Number{row.c}, asset, kUp), up);
            }
        }
    }
}

TEST(STAmount, mul_div_round_iou_sixteen_digits)
{
    TestAssets const assets;
    Number const one{1};
    Number const three{3};

    // 1 / 3 does not fit in 16 digits, so the direction decides the last digit.
    EXPECT_EQ(
        mulDivRound(one, one, three, assets.iou, kDown),
        STAmount(assets.usd, std::uint64_t{3'333'333'333'333'333}, -16));
    EXPECT_EQ(
        mulDivRound(one, one, three, assets.iou, kUp),
        STAmount(assets.usd, std::uint64_t{3'333'333'333'333'334}, -16));

    // An exact result needs no rounding in either direction.
    EXPECT_EQ(
        mulDivRound(Number{10}, one, Number{4}, assets.iou, kUp),
        STAmount(assets.usd, std::uint64_t{25}, -1));
    EXPECT_EQ(
        mulDivRound(Number{10}, one, Number{4}, assets.iou, kDown),
        STAmount(assets.usd, std::uint64_t{25}, -1));

    // Rounding up carries into a 17th digit and the result is coarsened once.
    NumberMantissaScaleGuard const sg(MantissaRange::MantissaScale::Large);
    Number const seventeenNines{std::int64_t{99'999'999'999'999'999}};
    EXPECT_EQ(
        mulDivRound(seventeenNines, one, Number{10}, assets.iou, kUp),
        STAmount(assets.usd, std::uint64_t{1}, 16));
    EXPECT_EQ(
        mulDivRound(seventeenNines, one, Number{10}, assets.iou, kDown),
        STAmount(assets.usd, std::uint64_t{9'999'999'999'999'999}, 0));
}

TEST(STAmount, mul_div_round_iou_below_smallest)
{
    TestAssets const assets;
    Number const one{1};
    // The smallest positive IOU: a 16-digit mantissa at the lowest exponent, 1e-81.
    STAmount const smallest{assets.usd, STAmount::kMinValue, STAmount::kMinOffset};
    STAmount const zero{assets.usd};

    // 3e-90 / 1e10 = 3e-100 is below the smallest IOU.
    Number const tiny{3, -90};
    Number const divisor{1, 10};
    EXPECT_EQ(mulDivRound(tiny, one, divisor, assets.iou, kDown), zero);
    EXPECT_EQ(mulDivRound(tiny, one, divisor, assets.iou, kUp), smallest);

    // 1e-50 * 1e-50 = 1e-100: the factors' exponents add below the smallest IOU.
    Number const small{1, -50};
    EXPECT_EQ(mulDivRound(small, small, one, assets.iou, kDown), zero);
    EXPECT_EQ(mulDivRound(small, small, one, assets.iou, kUp), smallest);

    // 1e-20000 is so far below that the quotient is not built at all.
    Number const vanishing{1, -20'000};
    EXPECT_EQ(mulDivRound(vanishing, one, one, assets.iou, kDown), zero);
    EXPECT_EQ(mulDivRound(vanishing, one, one, assets.iou, kUp), smallest);

    // A target grid between the value and the smallest IOU still rounds up to the smallest IOU,
    // since 1e-90 cannot be held.
    EXPECT_EQ(mulDivRound(tiny, one, divisor, assets.iou, kDown, -90), zero);
    EXPECT_EQ(mulDivRound(tiny, one, divisor, assets.iou, kUp, -90), smallest);

    // A target grid coarser than the smallest IOU rounds up to one unit of that grid.
    EXPECT_EQ(mulDivRound(tiny, one, divisor, assets.iou, kDown, -70), zero);
    EXPECT_EQ(
        mulDivRound(tiny, one, divisor, assets.iou, kUp, -70),
        STAmount(assets.usd, std::uint64_t{1}, -70));
}

TEST(STAmount, mul_div_round_iou_at_smallest)
{
    TestAssets const assets;
    Number const one{1};
    STAmount const smallest{assets.usd, STAmount::kMinValue, STAmount::kMinOffset};

    // Exactly the smallest IOU needs no rounding in either direction.
    Number const exact{1, -81};
    EXPECT_EQ(mulDivRound(exact, one, one, assets.iou, kDown), smallest);
    EXPECT_EQ(mulDivRound(exact, one, one, assets.iou, kUp), smallest);

    // 9.999999999999999e-82 is just below it: down is zero, up carries into the smallest IOU.
    Number const justBelow{9'999'999'999'999'999LL, -97};
    EXPECT_EQ(mulDivRound(justBelow, one, one, assets.iou, kDown), STAmount{assets.usd});
    EXPECT_EQ(mulDivRound(justBelow, one, one, assets.iou, kUp), smallest);

    // 1e-80 / 3 is above it and inexact: the last of 16 digits at the lowest exponent decides.
    Number const tenSmallest{1, -80};
    EXPECT_EQ(
        mulDivRound(tenSmallest, one, Number{3}, assets.iou, kDown),
        STAmount(assets.usd, std::uint64_t{3'333'333'333'333'333}, STAmount::kMinOffset));
    EXPECT_EQ(
        mulDivRound(tenSmallest, one, Number{3}, assets.iou, kUp),
        STAmount(assets.usd, std::uint64_t{3'333'333'333'333'334}, STAmount::kMinOffset));
}

TEST(STAmount, mul_div_round_target_exponent)
{
    TestAssets const assets;
    Number const two{2};
    Number const three{3};

    // 2 / 3 at the grid 10^-6, one rounding.
    EXPECT_EQ(
        mulDivRound(two, Number{1}, three, assets.iou, kDown, -6),
        STAmount(assets.usd, std::uint64_t{666'666}, -6));
    EXPECT_EQ(
        mulDivRound(two, Number{1}, three, assets.iou, kUp, -6),
        STAmount(assets.usd, std::uint64_t{666'667}, -6));

    // A target finer than 16 digits allow is coarsened to 16 digits.
    EXPECT_EQ(
        mulDivRound(Number{1'000'000'000}, Number{1}, three, assets.iou, kDown, -30),
        STAmount(assets.usd, std::uint64_t{3'333'333'333'333'333}, -7));

    // A target coarser than the value rounds the value to that grid.
    EXPECT_EQ(
        mulDivRound(Number{1'234}, Number{1}, Number{1}, assets.iou, kUp, 2),
        STAmount(assets.usd, std::uint64_t{1'300}, 0));
    EXPECT_EQ(
        mulDivRound(Number{1'234}, Number{1}, Number{1}, assets.mptAsset, kDown, 2),
        STAmount(assets.mpt, 1'200));

    // A target below zero means nothing to an integral asset.
    EXPECT_EQ(
        mulDivRound(Number{10}, Number{1}, three, assets.xrp, kUp, -6), STAmount(XRPAmount{4}));
}

TEST(STAmount, mul_div_round_zero)
{
    TestAssets const assets;
    for (Asset const& asset : {assets.iou, assets.mptAsset, assets.xrp})
    {
        for (auto const mode : {kDown, kUp})
        {
            for (auto const& [a, b] : {std::pair{0, 5}, std::pair{5, 0}, std::pair{0, 0}})
            {
                auto const result = mulDivRound(Number{a}, Number{b}, Number{3}, asset, mode);
                EXPECT_EQ(result.signum(), 0);
                EXPECT_FALSE(result.negative());
                EXPECT_TRUE(result.asset() == asset);
            }
        }
    }
}

TEST(STAmount, mul_div_round_division_by_zero)
{
    TestAssets const assets;
    for (Asset const& asset : {assets.iou, assets.mptAsset, assets.xrp})
    {
        EXPECT_THROW(
            (void)mulDivRound(Number{1}, Number{1}, Number{0}, asset, kDown), std::overflow_error);
        // c == 0 is rejected even when the numerator is zero.
        EXPECT_THROW(
            (void)mulDivRound(Number{0}, Number{1}, Number{0}, asset, kDown), std::overflow_error);
    }
}

TEST(STAmount, mul_div_round_negative_operand)
{
    TestAssets const assets;
    for (Asset const& asset : {assets.iou, assets.mptAsset, assets.xrp})
    {
        EXPECT_THROW(
            (void)mulDivRound(Number{-1}, Number{1}, Number{1}, asset, kDown), std::overflow_error);
        EXPECT_THROW(
            (void)mulDivRound(Number{1}, Number{-1}, Number{1}, asset, kDown), std::overflow_error);
        EXPECT_THROW(
            (void)mulDivRound(Number{1}, Number{1}, Number{-1}, asset, kUp), std::overflow_error);
        // A negative operand is rejected even when the other factor is zero.
        EXPECT_THROW(
            (void)mulDivRound(Number{0}, Number{1}, Number{-1}, asset, kDown), std::overflow_error);
    }
}

TEST(STAmount, mul_div_round_overflow)
{
    TestAssets const assets;
    NumberMantissaScaleGuard const sg(MantissaRange::MantissaScale::Large);

    // Past the largest MPT amount and XRP amount.
    EXPECT_THROW(
        (void)mulDivRound(Number{9, 18}, Number{2}, Number{1}, assets.mptAsset, kDown),
        std::overflow_error);
    EXPECT_THROW(
        (void)mulDivRound(Number{1, 18}, Number{1}, Number{1}, assets.xrp, kDown),
        std::overflow_error);
    EXPECT_THROW(
        (void)mulDivRound(Number{1, 30}, Number{1, 30}, Number{1}, assets.mptAsset, kDown),
        std::overflow_error);
    // A grid past 10^19 is beyond any integral amount.
    EXPECT_THROW(
        (void)mulDivRound(Number{1}, Number{1}, Number{1}, assets.mptAsset, kDown, 20),
        std::overflow_error);
    // So is a grid coarser than the largest IOU, below 10^(kMaxOffset + 16).
    EXPECT_THROW(
        (void)mulDivRound(
            Number{1}, Number{1}, Number{1}, assets.iou, kDown, STAmount::kMaxOffset + 17),
        std::overflow_error);
    // 10^(kMaxOffset + 16) is the coarsest grid: rounding down gives zero, rounding up overflows.
    EXPECT_EQ(
        mulDivRound(Number{1}, Number{1}, Number{1}, assets.iou, kDown, STAmount::kMaxOffset + 16),
        STAmount{assets.usd});
    EXPECT_THROW(
        (void)mulDivRound(
            Number{1}, Number{1}, Number{1}, assets.iou, kUp, STAmount::kMaxOffset + 16),
        std::overflow_error);
    // A grid above kMaxOffset still holds a value: 10^95 is 10^15 at the largest exponent.
    EXPECT_EQ(
        mulDivRound(Number{1}, Number{1}, Number{1}, assets.iou, kUp, STAmount::kMaxOffset + 15),
        STAmount(assets.usd, STAmount::kMinValue, STAmount::kMaxOffset));
    // Past the largest IOU exponent.
    EXPECT_THROW(
        (void)mulDivRound(Number{1, 60}, Number{1, 60}, Number{1}, assets.iou, kDown),
        std::overflow_error);
    // The largest values still fit.
    EXPECT_EQ(
        mulDivRound(
            Number{std::int64_t{kMaxMpTokenAmount}}, Number{1}, Number{1}, assets.mptAsset, kDown),
        STAmount(assets.mpt, kMaxMpTokenAmount));
    EXPECT_EQ(
        mulDivRound(
            Number{std::int64_t{STAmount::kMaxNativeN}}, Number{1}, Number{1}, assets.xrp, kDown),
        STAmount(XRPAmount{std::int64_t{STAmount::kMaxNativeN}}));
}

TEST(STAmount, mul_div_round_avoids_double_rounding)
{
    TestAssets const assets;
    NumberMantissaScaleGuard const sg(MantissaRange::MantissaScale::Large);

    // AT * S has 35 digits. Number multiplies, rounds to its mantissa, then divides, so
    // (AT * S) / S can be off by one from AT. mulDivRound keeps the product exact.
    Number const assetsTotal{std::int64_t{1'367'945'414'764'070'474}};
    Number const shares{std::int64_t{8'999'999'999'999'942}};
    EXPECT_NE((assetsTotal * shares) / shares, assetsTotal);
    for (auto const mode : {kDown, kUp})
    {
        EXPECT_EQ(
            mulDivRound(assetsTotal, shares, shares, assets.mptAsset, mode),
            STAmount(assets.mpt, std::int64_t{1'367'945'414'764'070'474}));
    }

    // (10^17 - 1) * 10^17 / (10^17 + 1) = 10^17 - 2 + 2 / (10^17 + 1): one rounding gives the
    // exact neighbour in each direction.
    Number const big{std::int64_t{99'999'999'999'999'999}};
    Number const bigPlus{std::int64_t{100'000'000'000'000'001}};
    Number const bigOne{std::int64_t{100'000'000'000'000'000}};
    EXPECT_EQ(
        mulDivRound(big, bigOne, bigPlus, assets.mptAsset, kDown),
        STAmount(assets.mpt, std::int64_t{99'999'999'999'999'998}));
    EXPECT_EQ(
        mulDivRound(big, bigOne, bigPlus, assets.mptAsset, kUp),
        STAmount(assets.mpt, std::int64_t{99'999'999'999'999'999}));
}
