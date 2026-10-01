#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
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

namespace xrpl {
namespace {

// Minimal bare LoanBroker SLE for helpers that only touch sfCoverAvailable
// and the broker's asset (e.g. creditToPosteriorBrokerCoverScale,
// getPosteriorBrokerCoverScale). Not a valid ledger entry on its own -- just
// enough for the helper's own field reads and type asserts.
std::shared_ptr<SLE>
makeBroker(Asset const& asset, Number const& coverAvailable)
{
    auto broker = std::make_shared<SLE>(keylet::loanBroker(uint256(2)));
    broker->at(sfCoverAvailable) = coverAvailable;
    associateAsset(*broker, asset);
    return broker;
}

TEST(LoanBrokerCover, broker_cover_scale_and_rounding)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};
    auto const vault = makeVault(iou, Number{0}, VaultVersion::FixedPrecision, 6);
    auto broker = makeBroker(iou, Number{9'999'999'999'999'999, -6});
    STAmount const inflow{iou, Number{21, -6}};

    EXPECT_EQ(detail::getPosteriorBrokerCoverScale(vault, broker, inflow), -5);
    EXPECT_EQ(
        detail::roundToPosteriorBrokerCoverScale(
            vault, broker, inflow, Number::RoundingMode::TowardsZero),
        STAmount(iou, Number{20, -6}));

    broker->at(sfCoverAvailable) = Number{1'000'000'000'000'001, -5};
    associateAsset(*broker, iou);
    STAmount const outflow{iou, -Number{11, -6}};
    EXPECT_EQ(detail::getPosteriorBrokerCoverScale(vault, broker, outflow), -6);
    EXPECT_EQ(
        detail::roundToPosteriorBrokerCoverScale(
            vault, broker, outflow, Number::RoundingMode::TowardsZero),
        outflow);

    // CoverAvailable exactly 1e10 (exponent -5). Withdrawing 1e-6 re-fines
    // to -6; the posterior rounded amount is 1e-6, which isZeroAtScale(-5)
    // would treat as zero.
    broker->at(sfCoverAvailable) = Number{1, 10};
    associateAsset(*broker, iou);
    STAmount const refine{iou, -Number{1, -6}};
    EXPECT_EQ(detail::getPosteriorBrokerCoverScale(vault, broker, refine), -6);
    EXPECT_EQ(
        detail::roundToPosteriorBrokerCoverScale(
            vault, broker, refine, Number::RoundingMode::TowardsZero),
        refine);
}

TEST(LoanBrokerCover, broker_cover_optional_inflow_boundaries)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};

    auto fixedIou = makeVault(iou, Number{0}, VaultVersion::FixedPrecision, 10);
    auto iouBroker = makeBroker(iou, Number{9, 5});
    STAmount const iouUnit{iou, Number{1, -10}};
    EXPECT_EQ(checkOptionalBrokerCoverInflow(fixedIou, iouBroker, STAmount{iou}), tesSUCCESS);
    EXPECT_EQ(checkOptionalBrokerCoverInflow(fixedIou, iouBroker, iouUnit), tecLIMIT_EXCEEDED);

    auto legacy = makeVault(iou, Number{0}, VaultVersion::CashBasis, 10);
    EXPECT_EQ(checkOptionalBrokerCoverInflow(legacy, iouBroker, iouUnit), tesSUCCESS);

    for (Asset const asset : {Asset{xrpIssue()}, Asset{MPTIssue{makeMptID(1, issuer.id())}}})
    {
        auto vault = makeVault(asset, Number{0}, VaultVersion::FixedPrecision, 0);
        auto broker = makeBroker(asset, Number{9, 15});
        EXPECT_EQ(
            checkOptionalBrokerCoverInflow(vault, broker, STAmount{asset, std::uint64_t{1}}),
            tecLIMIT_EXCEEDED);
    }
}

TEST(LoanBrokerCover, broker_cover_optional_inflow_scale_mismatch)
{
    // Base scale 6: CoverAvailable already carries 16 significant digits, so
    // any positive inflow needs a coarser posterior scale than the broker's
    // base scale. This must be rejected by the scale-mismatch check even
    // though the result stays well under the Open limit -- the two checks
    // are independent.
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};
    auto vault = makeVault(iou, Number{0}, VaultVersion::FixedPrecision, 6);
    auto broker = makeBroker(iou, Number{2, 10});
    STAmount const amount{iou, Number{1, -5}};
    EXPECT_EQ(checkOptionalBrokerCoverInflow(vault, broker, amount), tecLIMIT_EXCEEDED);
}

TEST(LoanBrokerCover, credit_to_posterior_broker_cover_scale_at_to_nearest_carry_cusp)
{
    // Base scale 6; CoverAvailable already has 16 significant digits, one
    // unit below the next power of ten. Number holds 19 digits, so the raw
    // sum CoverAvailable + raw is exact and never carries; the Downward
    // conversion to STAmount floors it at the unchanged base exponent. A
    // sub-unit raw amount (below the scale-6 grid) therefore credits
    // exactly zero.
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};
    Number const coverAvailable{9'999'999'999'999'999, -6};
    auto vault = makeVault(iou, Number{0}, VaultVersion::FixedPrecision, 6, Number{0}, Number{0});
    auto broker = makeBroker(iou, coverAvailable);

    STAmount const raw{iou, 5, -7};  // 0.0000005

    STAmount const credit =
        creditToPosteriorBrokerCoverScale(vault, broker, raw, Number::RoundingMode::Downward);

    // Sub-unit raw amount at the unchanged base exponent credits nothing.
    EXPECT_EQ(Number(credit), Number{0});

    // CA + credit must stay representable in 16 digits.
    Number const total = Number(coverAvailable) + Number(credit);
    EXPECT_EQ(Number(STAmount{iou, total}), total);
}

}  // namespace
}  // namespace xrpl
