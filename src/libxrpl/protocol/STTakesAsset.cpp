#include <xrpl/protocol/STTakesAsset.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STLedgerEntry.h>

namespace xrpl {

// Field metadata-driven sweep of every asset-typed STNumber on an SLE.
//
// Two SField metadata bits participate:
//
//   kSmdNeedsAsset       — always swept. This is the classic behaviour
//                          used by every asset-typed STNumber whose
//                          invariants tolerate STAmount (16-significant
//                          -digit) precision.
//
//   kSmdAssetPreLend11   — swept only when featureLendingProtocolV1_1
//                          is NOT enabled. Once the amendment activates
//                          the sweep is skipped and the field retains
//                          full Number (19-digit) precision.
//
// Currently only sfAssetsTotal carries kSmdAssetPreLend11. Under the
// dust-aware accounting introduced by featureLendingProtocolV1_1
// sfAssetsTotal must retain full precision so it can absorb the
// sub-STAmount residual that the custody trust line's sfDust also
// absorbs on every credit / debit; truncating on every write would
// silently drift the receivable identity
//
//   sfAssetsTotal - (sfAssetsAvailable + custodyLine.sfDust)
//       == Σ sfPrincipalOutstanding
//
// by exactly the accumulated dust. Skipping the sweep for this one
// field keeps the identity byte-for-byte exact across arbitrarily long
// dust-bearing sequences.
//
// isFeatureEnabled (see xrpl/protocol/Rules.h) reads the thread-local
// CurrentTransactionRulesGuard installed at the top of every
// transactor. When no rules are set (offline JSON parsing, tests that
// do not install a guard) the function returns false, so the negated
// predicate evaluates true and kSmdAssetPreLend11 fields are swept —
// exactly the legacy behaviour. This makes the gate invisible to
// call-sites: no signature changes and no edits at any of the
// associateAsset(sle, asset) call sites are needed.
void
associateAsset(SLE& sle, Asset const& asset)
{
    // Iterating by offset is the only way to get non-const references
    for (int i = 0; i < sle.getCount(); ++i)
    {
        STBase& entry = sle.getIndex(i);
        SField const& field = entry.getFName();
        bool const takeAsset = field.shouldMeta(SField::kSmdNeedsAsset) ||
            (field.shouldMeta(SField::kSmdAssetPreLend11) &&
             !isFeatureEnabled(featureLendingProtocolV1_1));
        if (takeAsset)
        {
            auto const type = entry.getSType();
            // If the field is not set or present, skip it.
            if (type == STI_NOTPRESENT)
                continue;

            // If the type doesn't downcast, then the flag shouldn't be on the
            // SField
            auto& ta = entry.downcast<STTakesAsset>();
            auto const style = sle.getStyle(ta.getFName());
            XRPL_ASSERT_PARTS(
                style != SoeInvalid, "xrpl::associateAsset", "valid template element style");

            XRPL_ASSERT_PARTS(
                style != SoeDefault || !ta.isDefault(),
                "xrpl::associateAsset",
                "non-default value");
            ta.associateAsset(asset);

            // associateAsset in derived classes may change the underlying
            // value, but it won't know anything about how the value relates to
            // the SLE. If the template element is soeDEFAULT, and the value
            // changed to the default value, remove the field.
            if (style == SoeDefault && ta.isDefault())
                sle.makeFieldAbsent(field);
        }
    }
}

}  // namespace xrpl
