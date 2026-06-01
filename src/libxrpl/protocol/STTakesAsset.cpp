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
    // Iterating by offset is the only way to get non-const references
    for (int i = 0; i < sle.getCount(); ++i)
    {
        STBase& entry = sle.getIndex(i);
        SField const& field = entry.getFName();
        if (field.shouldMeta(SField::kSmdNeedsAsset))
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
