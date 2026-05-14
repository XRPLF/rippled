/** @file
 *  Implements `associateAsset()`, which injects asset-type context into
 *  `sMD_NeedsAsset`-flagged fields of a ledger entry so that `STNumber`
 *  can round to the correct asset precision before serialization.
 *
 *  @note `STTakesAsset.h` must be included before `STLedgerEntry.h`.
 *      The header contains only a forward declaration of `STLedgerEntry`
 *      (to avoid a circular dependency), while this translation unit needs
 *      the full definition to call `getIndex()`, `getStyle()`, and
 *      `makeFieldAbsent()`.
 */
#include <xrpl/protocol/STTakesAsset.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STLedgerEntry.h>

namespace xrpl {

void
associateAsset(SLE& sle, Asset const& asset)
{
    // Offset-based iteration is the only path in STObject that yields
    // mutable STBase& references; iterator-based traversal is const-only.
    for (int i = 0; i < sle.getCount(); ++i)
    {
        STBase& entry = sle.getIndex(i);
        SField const& field = entry.getFName();
        if (field.shouldMeta(SField::kSMD_NEEDS_ASSET))
        {
            auto const type = entry.getSType();
            if (type == STI_NOTPRESENT)
                continue;

            // A failed downcast means kSMD_NEEDS_ASSET is set on a field
            // whose type does not derive from STTakesAsset — a programming
            // error in the field schema; downcast() throws to catch it early.
            auto& ta = entry.downcast<STTakesAsset>();
            auto const style = sle.getStyle(ta.getFName());
            XRPL_ASSERT_PARTS(
                style != SoeInvalid, "xrpl::associateAsset", "valid template element style");

            // soeDEFAULT fields are removed from the SLE when they equal
            // their default value, so a present soeDEFAULT field must be
            // non-default before association runs.
            XRPL_ASSERT_PARTS(
                style != SoeDefault || !ta.isDefault(),
                "xrpl::associateAsset",
                "non-default value");
            ta.associateAsset(asset);

            // Precision rounding inside associateAsset() may reduce the
            // value to zero.  A soeDEFAULT field at its default must not be
            // persisted in the ledger, so remove it if that happened.
            if (style == SoeDefault && ta.isDefault())
                sle.makeFieldAbsent(field);
        }
    }
}

}  // namespace xrpl
