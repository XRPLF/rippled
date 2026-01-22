#include <xrpl/protocol/STTakesAsset.h>
// Do not remove. Force STTakesAsset.h first
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
        if (field.shouldMeta(SField::sMD_NeedsAsset))
        {
            auto const type = entry.getSType();
            // If the field is not set or present, skip it.
            if (type == STI_NOTPRESENT)
                continue;
            // If the type doesn't downcast, then the flag shouldn't be on the
            // SField
            auto& ta = entry.downcast<STTakesAsset>();
            ta.associateAsset(asset);
        }
    }
}

}  // namespace xrpl
