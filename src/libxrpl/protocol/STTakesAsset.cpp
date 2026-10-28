#include <xrpl/protocol/STTakesAsset.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>

namespace xrpl {

namespace {

void
associateAssetFields(STObject& object, Asset const& asset)
{
    // Iterating by offset is the only way to get non-const references
    for (int i = 0; i < object.getCount(); ++i)
    {
        STBase& entry = object.getIndex(i);
        SField const& field = entry.getFName();
        if (entry.getSType() == STI_ARRAY)
        {
            // Inner objects carry their own templates, so their amounts round the same way.
            for (auto& element : entry.downcast<STArray>())
                associateAssetFields(element, asset);
            continue;
        }
        if (field.shouldMeta(SField::kSmdNeedsAsset))
        {
            auto const type = entry.getSType();
            // If the field is not set or present, skip it.
            if (type == STI_NOTPRESENT)
                continue;

            // If the type doesn't downcast, then the flag shouldn't be on the
            // SField
            auto& ta = entry.downcast<STTakesAsset>();
            auto const style = object.getStyle(ta.getFName());
            XRPL_ASSERT_PARTS(
                style != SoeInvalid, "xrpl::associateAsset", "valid template element style");

            XRPL_ASSERT_PARTS(
                style != SoeDefault || !ta.isDefault(),
                "xrpl::associateAsset",
                "non-default value");
            ta.associateAsset(asset);

            // associateAsset in derived classes may change the underlying
            // value, but it won't know anything about how the value relates to
            // the object. If the template element is soeDEFAULT, and the value
            // changed to the default value, remove the field.
            if (style == SoeDefault && ta.isDefault())
                object.makeFieldAbsent(field);
        }
    }
}

}  // namespace

void
associateAsset(SLE& sle, Asset const& asset)
{
    associateAssetFields(sle, asset);
}

}  // namespace xrpl
