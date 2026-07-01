#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {

/** Delete an offer.

    Requirements:
        The offer must exist.
        The caller must have already checked permissions.

    @param view The ApplyView to modify.
    @param sle The offer to delete.
    @param j Journal for logging.

    @return tesSUCCESS on success, otherwise an error code.
*/
// [[nodiscard]] // nodiscard commented out so Flow, BookTip and others compile.
TER
offerDelete(ApplyView& view, SLE::ref sle, beast::Journal j);

}  // namespace xrpl
