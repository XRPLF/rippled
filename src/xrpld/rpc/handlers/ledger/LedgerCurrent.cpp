/** @file
 *  Implements the `ledger_current` RPC handler.
 *
 *  Returns the sequence number of the open (in-progress) ledger — the ledger
 *  that is currently accepting transactions and has not yet been closed and
 *  validated. Registered with `NeedsCurrentLedger` and `Role::USER`.
 */

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

namespace xrpl {

/** Handle the `ledger_current` RPC command.
 *
 *  Reports the sequence number of the open ledger: the ledger currently
 *  accumulating transactions that has not yet been closed or validated.
 *  Only the sequence number is returned because an open ledger has no
 *  finalized state root and therefore no deterministic hash — contrast
 *  with `doLedgerClosed`, which returns both `ledger_index` and
 *  `ledger_hash`.
 *
 *  This handler accepts no client-supplied parameters; the `context` is
 *  used only to reach `ledgerMaster`.
 *
 *  @param context The RPC dispatch context; `context.ledgerMaster` provides
 *      the current open ledger index via `getCurrentLedgerIndex()`.
 *  @return A JSON object with a single field: `ledger_current_index`
 *      (uint32 sequence number of the open ledger).
 */
json::Value
doLedgerCurrent(RPC::JsonContext& context)
{
    json::Value jvResult;
    jvResult[jss::ledger_current_index] = context.ledgerMaster.getCurrentLedgerIndex();
    return jvResult;
}

}  // namespace xrpl
