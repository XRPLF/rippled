#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <utility>
#include <vector>

namespace xrpl {

/**
 * A repo's terms are fixed when it is struck, and it only ever moves forward.
 *
 * The economics of a repurchase agreement are agreed at create and accepted
 * against by the buyer. Nothing afterwards may restate them: not the parties,
 * not the collateral, not the price, not the rate, and not the dates. A repo
 * also moves in one direction only, from pending to active, so the start date
 * may appear once and may never change or be removed.
 *
 * A repo also leaves the ledger by exactly three paths: cancel, close and
 * default. Nothing else may delete one.
 */
class ValidRepo
{
    // <before, after>. before is unseated when the entry is being created.
    std::vector<std::pair<SLE::const_pointer, SLE::const_pointer>> repos_;

    // A repo leaves the ledger by exactly three transactions, so a deletion by
    // anything else is a defect wherever it came from.
    bool deleted_ = false;

public:
    void
    visitEntry(bool, SLE::const_ref, SLE::const_ref);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
