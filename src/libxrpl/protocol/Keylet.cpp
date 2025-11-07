#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STLedgerEntry.h>

namespace ripple {

bool
Keylet::check(STLedgerEntry const& sle) const
{
    XRPL_ASSERT(
        sle.getType() != ltANY || sle.getType() != ltCHILD,
        "ripple::Keylet::check : valid input type");

    if (type == ltANY)
        return true;

    if (type == ltCHILD)
        return sle.getType() != ltDIR_NODE;

    return sle.getType() == type && sle.key() == key;
}

}  // namespace ripple
