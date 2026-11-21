#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {

/**
 * Options for offerDelete().
 */
struct OfferDeleteOptions
{
    EraseMode eraseMode = EraseMode::Normal;
};

/**
 * Delete an offer.
 *
 * Requirements:
 *     The offer must exist.
 *     The caller must have already checked permissions.
 *
 * @param view The ApplyView to modify.
 * @param sle The offer to delete.
 * @param j Journal for logging.
 * @param options Options for the delete.
 *
 * @return tesSUCCESS on success, otherwise an error code.
 */
// [[nodiscard]] // nodiscard commented out so Flow, BookTip and others compile.
TER
offerDelete(ApplyView& view, SLE::Ref sle, beast::Journal j, OfferDeleteOptions options = {});

}  // namespace xrpl
