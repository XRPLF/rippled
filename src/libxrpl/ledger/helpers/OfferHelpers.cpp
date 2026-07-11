#include <xrpl/ledger/helpers/OfferHelpers.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/SLEWrappers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>  // IWYU pragma: keep
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>  // IWYU pragma: keep
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {

TER
offerDelete(ApplyView& view, SLE::ref sle, beast::Journal j)
{
    if (!sle)
        return tesSUCCESS;

    // Unlink the offer from its owner directory and every order-book page it
    // sits in (including a hybrid offer's additional books), decrement the
    // owner's OwnerCount (refunding a reserve sponsor when present), and erase
    // it. See OfferEntry::destroy().
    OfferEntry<ApplyView> offer{sle, view, j};
    return offer.destroy();
}

}  // namespace xrpl
