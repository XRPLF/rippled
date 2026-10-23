#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <protocol/VaultTestHelpers.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_set>

namespace xrpl {
namespace {

TEST(VaultGrid, base_and_live_scale)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};

    auto const legacy = makeVault(iou, Number{1'000'000}, VaultVersion::Legacy, 6);
    EXPECT_EQ(getVaultScale(legacy), -9);
    EXPECT_EQ(getVaultBaseScale(legacy), getVaultScale(legacy));

    auto const empty = makeVault(iou, Number{0}, VaultVersion::FixedPrecision, 6);
    EXPECT_EQ(getVaultScale(empty), -6);
    EXPECT_EQ(getVaultBaseScale(empty), -6);

    auto const small = makeVault(iou, Number{1'000'000}, VaultVersion::FixedPrecision, 6);
    EXPECT_EQ(getVaultScale(small), -6);
    EXPECT_EQ(getVaultBaseScale(small), -6);

    auto const coarsened = makeVault(iou, Number{10'000'000'000}, VaultVersion::FixedPrecision, 6);
    EXPECT_GT(getVaultScale(coarsened), getVaultBaseScale(coarsened));
    EXPECT_EQ(getVaultBaseScale(coarsened), -6);
}

TEST(VaultGrid, pre_v12_behavior_is_preserved)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};
    Number const assetsTotal{1'000'000};
    STAmount const onGrid{iou, Number{2, -9}};
    STAmount const dust{iou, Number{4, -10}};
    STAmount const overOpen{iou, Number{10, 9}};
    auto const downward = Number::RoundingMode::Downward;

    auto const legacy = makeVault(iou, assetsTotal, VaultVersion::Legacy);
    int const expectedLive = getVaultScale(legacy);
    int const expectedPosterior = detail::getPosteriorVaultScale(legacy, onGrid);
    STAmount const expectedPosteriorRound = roundToPosteriorVaultScale(legacy, dust, downward);

    for (auto const version : {
             std::optional<VaultVersion>{},
             std::optional{VaultVersion::Legacy},
             std::optional{VaultVersion::CashBasis},
         })
    {
        auto const vault = makeVault(iou, assetsTotal, version);
        EXPECT_EQ(getVaultScale(vault), expectedLive);
        EXPECT_EQ(getVaultBaseScale(vault), expectedLive);
        EXPECT_EQ(detail::getPosteriorVaultScale(vault, onGrid), expectedPosterior);
        EXPECT_EQ(roundToPosteriorVaultScale(vault, dust, downward), expectedPosteriorRound);
        EXPECT_EQ(checkOptionalVaultInflow(vault, overOpen), tesSUCCESS);
    }
}

TEST(VaultGrid, pre_v12_credit_clamp_floors_posterior_total)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};
    Number const assetsTotal{9'999'999'999'999'999LL, -15};
    STAmount const delta{iou, Number{5}};
    STAmount const expected{iou, Number{4'999'999'999'999'991LL, -15}};

    for (auto const version : {
             std::optional<VaultVersion>{},
             std::optional{VaultVersion::Legacy},
             std::optional{VaultVersion::CashBasis},
         })
    {
        auto const result = clampToAssetsTotalScale(makeVault(iou, assetsTotal, version), delta);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, expected);
    }
}

TEST(VaultGrid, integral_scale_is_zero)
{
    auto const vault = makeVault(xrpIssue(), Number{1'000}, VaultVersion::FixedPrecision, 0);
    STAmount const delta{xrpIssue(), 7};

    EXPECT_EQ(getVaultScale(vault), 0);
    EXPECT_EQ(getVaultBaseScale(vault), 0);
    EXPECT_EQ(detail::getPosteriorVaultScale(vault, delta), 0);
    EXPECT_EQ(roundToPosteriorVaultScale(vault, delta, Number::RoundingMode::TowardsZero), delta);
}

TEST(VaultGrid, posterior_scale_rounds_delta)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};
    auto const vault =
        makeVault(iou, Number{9'999'999'999'999'999, -6}, VaultVersion::FixedPrecision, 6);
    STAmount const delta{iou, Number{21, -6}};

    EXPECT_EQ(getVaultScale(vault), -6);
    EXPECT_EQ(detail::getPosteriorVaultScale(vault, delta), -5);
    EXPECT_EQ(
        roundToPosteriorVaultScale(vault, delta, Number::RoundingMode::TowardsZero),
        STAmount(iou, Number{20, -6}));
}

TEST(VaultGrid, posterior_scale_rejects_dust_at_call_site)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};
    auto const vault = makeVault(iou, Number{10'000'000'000}, VaultVersion::FixedPrecision, 6);
    STAmount const dust{iou, Number{1, -6}};

    EXPECT_EQ(
        roundToPosteriorVaultScale(vault, dust, Number::RoundingMode::TowardsZero), beast::kZero);
}

TEST(VaultGrid, posterior_outflow_can_refine_scale)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};
    auto const vault =
        makeVault(iou, Number{1'000'000'000'000'001, -5}, VaultVersion::FixedPrecision, 6);
    STAmount const delta{iou, -Number{11, -6}};

    EXPECT_EQ(getVaultScale(vault), -5);
    EXPECT_EQ(detail::getPosteriorVaultScale(vault, delta), -6);
    EXPECT_EQ(roundToPosteriorVaultScale(vault, delta, Number::RoundingMode::TowardsZero), delta);
}

TEST(VaultGrid, optional_inflow_capacity_boundaries)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};

    auto const fixedIou = makeVault(iou, Number{9, 5}, VaultVersion::FixedPrecision, 10);
    STAmount const iouDust{iou, Number{1, -10}};
    EXPECT_EQ(getVaultOpenLimit(fixedIou), (Number{9, 5}));
    EXPECT_EQ(checkOptionalVaultInflow(fixedIou, STAmount{iou}), tesSUCCESS);
    EXPECT_EQ(checkOptionalVaultInflow(fixedIou, iouDust), tecLIMIT_EXCEEDED);

    auto const fixedXrp = makeVault(xrpIssue(), Number{9, 15}, VaultVersion::FixedPrecision, 0);
    STAmount const xrpUnit{xrpIssue(), 1};
    EXPECT_EQ(getVaultOpenLimit(fixedXrp), (Number{9, 15}));
    EXPECT_EQ(checkOptionalVaultInflow(fixedXrp, STAmount{xrpIssue()}), tesSUCCESS);
    EXPECT_EQ(checkOptionalVaultInflow(fixedXrp, xrpUnit), tecLIMIT_EXCEEDED);

    auto const legacy = makeVault(iou, Number{10, 5}, VaultVersion::Legacy, 10);
    EXPECT_EQ(checkOptionalVaultInflow(legacy, iouDust), tesSUCCESS);

    auto const coarsening =
        makeVault(iou, Number{9'999'999'999'999'999, -6}, VaultVersion::FixedPrecision, 6);
    STAmount const coarseningDelta{iou, Number{21, -6}};
    EXPECT_EQ(checkOptionalVaultInflow(coarsening, coarseningDelta), tecLIMIT_EXCEEDED);
}

TEST(VaultGrid, optional_inflow_includes_yield_unrealized)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};
    auto const vault = makeVault(iou, Number{8'999'999'999}, VaultVersion::FixedPrecision, 6);
    STAmount const amount{iou, Number{1}};

    EXPECT_EQ(checkOptionalVaultInflow(vault, amount), tesSUCCESS);

    vault->at(sfYieldUnrealized) = Number{1};
    associateAsset(*vault, iou);
    EXPECT_EQ(checkOptionalVaultInflow(vault, amount), tecLIMIT_EXCEEDED);
}

// AssetsAvailable + raw needs 20 digits. Under the ToNearest ambient mode the
// sum would round up to the next 10^-6 before the Downward floor, crediting
// 0.000001 for a raw 0.0000009996.
TEST(VaultGrid, credit_floors_sum_in_requested_mode)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};
    auto const vault = makeVault(
        iou,
        Number{0},
        VaultVersion::FixedPrecision,
        6,
        Number{0},
        Number{1'234'567'890'123'456LL, -6});
    Number const raw{9'996, -10};

    NumberRoundModeGuard const ambient(Number::RoundingMode::ToNearest);
    STAmount const credit =
        creditToPosteriorAvailableScale(vault, raw, Number::RoundingMode::Downward);
    EXPECT_EQ(credit, STAmount(iou, 0));
    EXPECT_LE(Number(credit), raw);
}

TEST(VaultGrid, is_on_vault_base_grid)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};
    int const baseScale = vaultBaseScale(iou, 6);

    struct Row
    {
        Number value;
        bool expected{};
    };
    Row const rows[] = {
        {.value = Number{0}, .expected = true},
        {.value = Number{1}, .expected = true},
        {.value = Number{1, -6}, .expected = true},
        {.value = Number{1, 20}, .expected = true},
        {.value = Number{1'234'567'890'123'456LL, -6}, .expected = true},
        // A digit below baseScale.
        {.value = Number{1, -7}, .expected = false},
        {.value = Number{15, -7}, .expected = false},
        // A 17th significant digit, on the base grid but not an STAmount.
        {.value = Number{12'345'678'901'234'567LL, 0}, .expected = false},
    };
    for (auto const& row : rows)
        EXPECT_EQ(isOnVaultBaseGrid(iou, row.value, baseScale), row.expected) << row.value;

    EXPECT_EQ(vaultBaseScale(xrpIssue(), 6), 0);
    EXPECT_TRUE(isOnVaultBaseGrid(xrpIssue(), Number{7}, 0));

    // Out of range for the asset: rejected instead of throwing.
    EXPECT_FALSE(isOnVaultBaseGrid(
        xrpIssue(), Number{static_cast<std::int64_t>(STAmount::kMaxNativeN)} + 1, 0));
    EXPECT_FALSE(isOnVaultBaseGrid(iou, Number{1, 100}, baseScale));
}

TEST(VaultGrid, clamp_vault_outflow)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};
    Asset const mpt{MPTIssue{makeMptID(1, issuer.id())}};

    struct Row
    {
        char const* name = nullptr;
        Asset asset;
        Number available;
        Number delta;
        std::optional<Number> expected;  // nullopt: tecPRECISION_LOSS
    };
    Row const rows[] = {
        {
            .name = "XRP outflow is returned unrounded",
            .asset = xrpIssue(),
            .available = Number{1'000},
            .delta = Number{-7},
            .expected = Number{7},
        },
        {
            .name = "MPT outflow is returned unrounded",
            .asset = mpt,
            .available = Number{1'000},
            .delta = Number{-7},
            .expected = Number{7},
        },
        {
            .name = "IOU outflow rounds towards zero at the posterior scale",
            .asset = iou,
            .available = Number{1'000'000},
            .delta = Number{-15, -7},
            .expected = Number{1, -6},
        },
        {
            .name = "IOU outflow below one unit at the posterior scale is rejected",
            .asset = iou,
            .available = Number{1'000'000},
            .delta = Number{-4, -7},
            .expected = std::nullopt,
        },
    };
    for (auto const& row : rows)
    {
        auto const vault = makeVault(
            row.asset,
            row.available,
            VaultVersion::FixedPrecision,
            row.asset.integral() ? 0 : 6,
            Number{0},
            row.available);
        auto const result = clampVaultOutflow(vault, STAmount{row.asset, row.delta});
        if (row.expected)
        {
            ASSERT_TRUE(result.has_value()) << row.name;
            EXPECT_EQ(*result, (STAmount{row.asset, *row.expected})) << row.name;
        }
        else
        {
            ASSERT_FALSE(result.has_value()) << row.name;
            EXPECT_EQ(result.error(), tecPRECISION_LOSS) << row.name;
        }
    }
}

TEST(VaultGrid, get_vault_default_version)
{
    struct Row
    {
        char const* name = nullptr;
        std::unordered_set<UInt256, beast::Uhash<>> enabled;
        VaultVersion expected;
    };
    Row const rows[] = {
        {.name = "no lending amendments: Legacy", .enabled = {}, .expected = VaultVersion::Legacy},
        {
            .name = "LendingProtocolV1_1: CashBasis",
            .enabled = {featureLendingProtocolV1_1},
            .expected = VaultVersion::CashBasis,
        },
        {
            .name = "V1_1 and V1_2 without the cleanups: CashBasis",
            .enabled = {featureLendingProtocolV1_1, featureLendingProtocolV1_2},
            .expected = VaultVersion::CashBasis,
        },
        {
            .name = "V1_1, V1_2 and fixCleanup3_2_0 without fixCleanup3_4_0: CashBasis",
            .enabled = {featureLendingProtocolV1_1, featureLendingProtocolV1_2, fixCleanup3_2_0},
            .expected = VaultVersion::CashBasis,
        },
        {
            .name = "V1_1, V1_2 and fixCleanup3_4_0: FixedPrecision",
            .enabled = {featureLendingProtocolV1_1, featureLendingProtocolV1_2, fixCleanup3_4_0},
            .expected = VaultVersion::FixedPrecision,
        },
        {
            .name = "V1_2 and both cleanups without V1_1: Legacy",
            .enabled = {featureLendingProtocolV1_2, fixCleanup3_2_0, fixCleanup3_4_0},
            .expected = VaultVersion::Legacy,
        },
        {
            .name = "V1_1, V1_2 and both cleanups: FixedPrecision",
            .enabled =
                {
                    featureLendingProtocolV1_1,
                    featureLendingProtocolV1_2,
                    fixCleanup3_2_0,
                    fixCleanup3_4_0,
                },
            .expected = VaultVersion::FixedPrecision,
        },
    };
    for (auto const& row : rows)
    {
        // Rules keeps a reference to the preset set; row.enabled outlives it.
        Rules const rules{row.enabled};
        EXPECT_EQ(getVaultDefaultVersion(rules), row.expected) << row.name;
    }
}

}  // namespace
}  // namespace xrpl
