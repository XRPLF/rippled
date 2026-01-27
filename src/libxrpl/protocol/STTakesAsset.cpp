#include <xrpl/protocol/STTakesAsset.h>
// Do not remove. Force STTakesAsset.h first
#include <xrpl/protocol/STLedgerEntry.h>

namespace ripple {

void
associateAsset(SLE& sle, Asset const& asset)
{
    // Iterating by offset is the only way to get non-const references
    for (int i = 0; i < sle.getCount(); ++i)
    {
        STBase& entry = sle.getIndex(i);
        SField const& field = entry.getFName();
        if (field.shouldMeta(SField::sMD_NeedsAsset))
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
                style != soeINVALID,
                "ripple::associateAsset",
                "valid template element style");

            XRPL_ASSERT_PARTS(
                style != soeDEFAULT || !ta.isDefault(),
                "ripple::associateAsset",
                "non-default value");
            ta.associateAsset(asset);

            // associateAsset in derived classes may change the underlying
            // value, but it won't know anything about how the value relates to
            // the SLE. If the template element is soeDEFAULT, and the value
            // changed to the default value, remove the field.
            if (style == soeDEFAULT && ta.isDefault())
                sle.makeFieldAbsent(field);
        }
    }
}

}  // namespace ripple
