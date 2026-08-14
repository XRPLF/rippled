// Unit tests for the associateAsset(SLE&, Asset const&) sweep and its
// two metadata gates:
//   - kSmdNeedsAsset:      always swept.
//   - kSmdAssetPreLend11:  swept only when featureLendingProtocolV1_1
//                          is NOT enabled.
//
// The interesting bit exercised here is the amendment-driven gate on
// kSmdAssetPreLend11: sfAssetsTotal (the only field currently tagged
// with it) must retain full Number precision under Lend11 so that its
// value can absorb the sub-STAmount residual mirrored on the custody
// trust line's sfDust. Pre-amendment behaviour is byte-for-byte
// identical to kSmdNeedsAsset.

#include <xrpl/protocol/STTakesAsset.h>

#include <xrpl/basics/Number.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/UintTypes.h>

#include <gtest/gtest.h>

#include <memory>
#include <unordered_set>

using namespace xrpl;

namespace {

// Rules helpers — construct with / without featureLendingProtocolV1_1.
Rules
noFeatures()
{
    static std::unordered_set<uint256, beast::Uhash<>> const kNone;
    return Rules{kNone};
}

Rules
lend11Only()
{
    static std::unordered_set<uint256, beast::Uhash<>> const kFeatures{featureLendingProtocolV1_1};
    return Rules{kFeatures};
}

// A minimal Vault-shaped SLE with sfAssetsTotal populated at 19-digit
// Number precision. We choose a value whose STAmount projection would
// truncate — 1'234'567'890'123'456.789 → STAmount cannot represent the
// trailing digits, so a sweep would visibly change the value.
std::shared_ptr<SLE>
makeVault(Number const& assetsTotal)
{
    auto sle = std::make_shared<SLE>(ltVAULT, uint256{7u});
    sle->at(sfAssetsTotal) = assetsTotal;
    return sle;
}

// An IOU asset with a fresh issuer/currency so scale() has meaningful
// context.
Asset
makeIouAsset()
{
    // Arbitrary issuer AccountID with a non-zero low byte so it is
    // formally valid; the exact value does not matter for the sweep.
    AccountID issuer;
    issuer.data()[0] = 1;
    return Asset{Issue{toCurrency("USD"), issuer}};
}

}  // namespace

// SField-metadata sanity: the field flags are as expected before we
// exercise the runtime gate.
TEST(STTakesAsset, sfAssetsTotalCarriesPreLend11Bit)
{
    EXPECT_FALSE(sfAssetsTotal.shouldMeta(SField::kSmdNeedsAsset));
    EXPECT_TRUE(sfAssetsTotal.shouldMeta(SField::kSmdAssetPreLend11));
}

TEST(STTakesAsset, sfAssetsAvailableStaysOnKSmdNeedsAsset)
{
    EXPECT_TRUE(sfAssetsAvailable.shouldMeta(SField::kSmdNeedsAsset));
    EXPECT_FALSE(sfAssetsAvailable.shouldMeta(SField::kSmdAssetPreLend11));
}

// A field tagged only kSmdNeedsAsset is swept regardless of amendment
// state. sfAssetsAvailable carries this exact metadata combination.
TEST(STTakesAsset, kSmdNeedsAssetSweepsInBothAmendmentStates)
{
    auto const asset = makeIouAsset();
    // A value with more than 16 significant digits so we can observe
    // the sweep's truncation. Choose one that STAmount will visibly
    // shorten.
    Number const finePrecision{123'456'789'012'345'678, -18};  // 0.123456789012345678

    {
        auto sle = std::make_shared<SLE>(ltVAULT, uint256{9u});
        sle->at(sfAssetsAvailable) = finePrecision;
        {
            CurrentTransactionRulesGuard const rg{noFeatures()};
            associateAsset(*sle, asset);
        }
        Number const swept = sle->at(sfAssetsAvailable);
        EXPECT_NE(swept, finePrecision);  // Truncated.
    }

    {
        auto sle = std::make_shared<SLE>(ltVAULT, uint256{9u});
        sle->at(sfAssetsAvailable) = finePrecision;
        {
            CurrentTransactionRulesGuard const rg{lend11Only()};
            associateAsset(*sle, asset);
        }
        Number const swept = sle->at(sfAssetsAvailable);
        EXPECT_NE(swept, finePrecision);  // Still truncated post-Lend11.
    }
}

// A field tagged only kSmdAssetPreLend11 is swept pre-amendment and
// skipped post-amendment. sfAssetsTotal carries this exact metadata
// combination.
TEST(STTakesAsset, kSmdAssetPreLend11SweptOnlyPreAmendment)
{
    auto const asset = makeIouAsset();
    Number const finePrecision{123'456'789'012'345'678, -18};

    // Pre-amendment: swept — value truncates.
    {
        auto sle = makeVault(finePrecision);
        {
            CurrentTransactionRulesGuard const rg{noFeatures()};
            associateAsset(*sle, asset);
        }
        Number const swept = sle->at(sfAssetsTotal);
        EXPECT_NE(swept, finePrecision);
    }

    // Post-amendment: skipped — value survives untouched.
    {
        auto sle = makeVault(finePrecision);
        {
            CurrentTransactionRulesGuard const rg{lend11Only()};
            associateAsset(*sle, asset);
        }
        Number const survived = sle->at(sfAssetsTotal);
        EXPECT_EQ(survived, finePrecision);
    }
}

// A round-trip: sweep pre-amendment, then verify the value is stable
// (idempotent) — a second sweep leaves it unchanged. This mirrors the
// legacy semantics closely so a regression that flips the gate would
// still show up as identical bytes across the two.
TEST(STTakesAsset, preAmendmentSweepIsIdempotent)
{
    auto const asset = makeIouAsset();
    Number const finePrecision{123'456'789'012'345'678, -18};

    auto sle = makeVault(finePrecision);
    {
        CurrentTransactionRulesGuard const rg{noFeatures()};
        associateAsset(*sle, asset);
    }
    Number const afterFirst = sle->at(sfAssetsTotal);
    {
        CurrentTransactionRulesGuard const rg{noFeatures()};
        associateAsset(*sle, asset);
    }
    Number const afterSecond = sle->at(sfAssetsTotal);
    EXPECT_EQ(afterFirst, afterSecond);
}

// When no CurrentTransactionRulesGuard is installed (e.g. offline
// JSON parsing, tests that do not install rules), isFeatureEnabled
// returns false, so the negated predicate evaluates true and
// kSmdAssetPreLend11 fields are swept — matching the legacy
// pre-amendment behaviour.
TEST(STTakesAsset, noRulesInstalledFallsBackToLegacySweep)
{
    auto const asset = makeIouAsset();
    Number const finePrecision{123'456'789'012'345'678, -18};

    auto sle = makeVault(finePrecision);
    // Deliberately no guard here.
    associateAsset(*sle, asset);
    Number const swept = sle->at(sfAssetsTotal);
    EXPECT_NE(swept, finePrecision);
}
