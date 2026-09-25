#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace xrpl {
namespace {

std::shared_ptr<SLE>
makeVault(
    Asset const& asset,
    Number const& assetsTotal,
    std::optional<VaultVersion> version,
    std::uint8_t scaleValue = kVaultDefaultIouScale)
{
    auto vault = std::make_shared<SLE>(keylet::vault(uint256(1)));
    vault->setFieldIssue(sfAsset, STIssue{sfAsset, asset});
    vault->at(sfAssetsTotal) = assetsTotal;
    if (!asset.integral())
        vault->at(sfScale) = scaleValue;
    if (version)
        vault->at(sfLEVersion) = std::to_underlying(*version);
    associateAsset(*vault, asset);
    return vault;
}

TEST(VaultGrid, BaseAndLiveScale)
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

TEST(VaultGrid, PreV12BehaviorIsPreserved)
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
    int const expectedPosterior = getPosteriorVaultScale(legacy, onGrid);
    STAmount const expectedLiveRound = roundToVaultScale(legacy, onGrid, downward);
    STAmount const expectedPosteriorRound = roundToPosteriorVaultScale(legacy, dust, downward);

    for (auto const version :
         {std::optional<VaultVersion>{},
          std::optional{VaultVersion::Legacy},
          std::optional{VaultVersion::CashBasis}})
    {
        auto const vault = makeVault(iou, assetsTotal, version);
        EXPECT_EQ(getVaultScale(vault), expectedLive);
        EXPECT_EQ(getVaultBaseScale(vault), expectedLive);
        EXPECT_EQ(getPosteriorVaultScale(vault, onGrid), expectedPosterior);
        EXPECT_EQ(roundToVaultScale(vault, onGrid, downward), expectedLiveRound);
        EXPECT_EQ(roundToPosteriorVaultScale(vault, dust, downward), expectedPosteriorRound);
        EXPECT_EQ(checkOptionalVaultInflow(vault, overOpen), tesSUCCESS);
    }
}

TEST(VaultGrid, PreV12CreditClampFloorsPosteriorTotal)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};
    Number const assetsTotal{9'999'999'999'999'999LL, -15};
    STAmount const delta{iou, Number{5}};
    STAmount const expected{iou, Number{4'999'999'999'999'991LL, -15}};

    for (auto const version :
         {std::optional<VaultVersion>{},
          std::optional{VaultVersion::Legacy},
          std::optional{VaultVersion::CashBasis}})
    {
        auto const result = clampToAssetsTotalScale(makeVault(iou, assetsTotal, version), delta);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, expected);
    }
}

TEST(VaultGrid, IntegralScaleIsZero)
{
    auto const vault = makeVault(xrpIssue(), Number{1'000}, VaultVersion::FixedPrecision, 0);
    STAmount const delta{xrpIssue(), 7};

    EXPECT_EQ(getVaultScale(vault), 0);
    EXPECT_EQ(getVaultBaseScale(vault), 0);
    EXPECT_EQ(getPosteriorVaultScale(vault, delta), 0);
    EXPECT_EQ(roundToPosteriorVaultScale(vault, delta, Number::RoundingMode::TowardsZero), delta);
}

TEST(VaultGrid, PosteriorScaleRoundsDelta)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};
    auto const vault =
        makeVault(iou, Number{9'999'999'999'999'999, -6}, VaultVersion::FixedPrecision, 6);
    STAmount const delta{iou, Number{21, -6}};

    EXPECT_EQ(getVaultScale(vault), -6);
    EXPECT_EQ(getPosteriorVaultScale(vault, delta), -5);
    EXPECT_EQ(roundToVaultScale(vault, delta, Number::RoundingMode::TowardsZero), delta);
    EXPECT_EQ(
        roundToPosteriorVaultScale(vault, delta, Number::RoundingMode::TowardsZero),
        STAmount(iou, Number{20, -6}));
}

TEST(VaultGrid, PosteriorScaleRejectsDustAtCallSite)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};
    auto const vault = makeVault(iou, Number{10'000'000'000}, VaultVersion::FixedPrecision, 6);
    STAmount const dust{iou, Number{1, -6}};

    EXPECT_EQ(
        roundToPosteriorVaultScale(vault, dust, Number::RoundingMode::TowardsZero), beast::kZero);
}

TEST(VaultGrid, PosteriorOutflowCanRefineScale)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};
    auto const vault =
        makeVault(iou, Number{1'000'000'000'000'001, -5}, VaultVersion::FixedPrecision, 6);
    STAmount const delta{iou, -Number{11, -6}};

    EXPECT_EQ(getVaultScale(vault), -5);
    EXPECT_EQ(getPosteriorVaultScale(vault, delta), -6);
    EXPECT_EQ(roundToPosteriorVaultScale(vault, delta, Number::RoundingMode::TowardsZero), delta);
}

TEST(VaultGrid, OptionalInflowCapacityBoundaries)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};

    auto fixedIou = makeVault(iou, Number{9, 5}, VaultVersion::FixedPrecision, 10);
    STAmount const iouDust{iou, Number{1, -10}};
    EXPECT_EQ(getVaultOpenLimit(fixedIou), (Number{9, 5}));
    EXPECT_EQ(checkOptionalVaultInflow(fixedIou, STAmount{iou}), tesSUCCESS);
    EXPECT_EQ(checkOptionalVaultInflow(fixedIou, iouDust), tecLIMIT_EXCEEDED);

    auto fixedXrp = makeVault(xrpIssue(), Number{9, 15}, VaultVersion::FixedPrecision, 0);
    STAmount const xrpUnit{xrpIssue(), 1};
    EXPECT_EQ(getVaultOpenLimit(fixedXrp), (Number{9, 15}));
    EXPECT_EQ(checkOptionalVaultInflow(fixedXrp, STAmount{xrpIssue()}), tesSUCCESS);
    EXPECT_EQ(checkOptionalVaultInflow(fixedXrp, xrpUnit), tecLIMIT_EXCEEDED);

    auto legacy = makeVault(iou, Number{10, 5}, VaultVersion::Legacy, 10);
    EXPECT_EQ(checkOptionalVaultInflow(legacy, iouDust), tesSUCCESS);

    auto coarsening =
        makeVault(iou, Number{9'999'999'999'999'999, -6}, VaultVersion::FixedPrecision, 6);
    STAmount const coarseningDelta{iou, Number{21, -6}};
    EXPECT_EQ(checkOptionalVaultInflow(coarsening, coarseningDelta), tecLIMIT_EXCEEDED);
}

TEST(VaultGrid, OptionalInflowIncludesYieldUnrealized)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};
    auto vault = makeVault(iou, Number{8'999'999'999}, VaultVersion::FixedPrecision, 6);
    STAmount const amount{iou, Number{1}};

    EXPECT_EQ(checkOptionalVaultInflow(vault, amount), tesSUCCESS);

    vault->at(sfYieldUnrealized) = Number{1};
    associateAsset(*vault, iou);
    EXPECT_EQ(checkOptionalVaultInflow(vault, amount), tecLIMIT_EXCEEDED);
}

}  // namespace
}  // namespace xrpl
