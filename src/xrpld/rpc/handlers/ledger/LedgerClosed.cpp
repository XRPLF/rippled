/** @file
 *  Implements the `ledger_closed` RPC handler.
 *
 *  Returns the sequence number and hash of the most recently closed ledger —
 *  the ledger that has completed consensus but may not yet have a validation
 *  quorum. Registered with `NEEDS_CLOSED_LEDGER` and `Role::USER`.
 */

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

namespace xrpl {

/** Handle the `ledger_closed` RPC command.
 *
 *  Returns the sequence number (`ledger_index`) and hash (`ledger_hash`) of
 *  the most recently closed ledger. The closed ledger has completed the
 *  consensus process and is immutable, but a validation supermajority has not
 *  necessarily been reached yet — it sits between the open current ledger and
 *  the fully validated one.
 *
 *  This handler accepts no client-supplied parameters; the `context` is used
 *  only to reach `ledgerMaster`.
 *
 *  @param context The RPC dispatch context; `context.ledgerMaster` provides
 *      the closed ledger reference.
 *  @return A JSON object with exactly two fields: `ledger_index` (uint32
 *      sequence number) and `ledger_hash` (lowercase hex string).
 *  @note The `NEEDS_CLOSED_LEDGER` precondition guarantees a closed ledger
 *      exists before this handler is invoked. The internal `XRPL_ASSERT`
 *      therefore guards against a framework invariant violation, not a
 *      normal client error path.
 */
json::Value
doLedgerClosed(RPC::JsonContext& context)
{
    auto ledger = context.ledgerMaster.getClosedLedger();
    XRPL_ASSERT(ledger, "xrpl::doLedgerClosed : non-null closed ledger");

    json::Value jvResult;
    jvResult[jss::ledger_index] = ledger->header().seq;
    jvResult[jss::ledger_hash] = to_string(ledger->header().hash);

    return jvResult;
}

}  // namespace xrpl
