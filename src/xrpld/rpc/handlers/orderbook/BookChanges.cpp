/** @file
 *  RPC handler for the `book_changes` API method.
 *
 *  Resolves the caller's ledger selector and delegates to
 *  `RPC::computeBookChanges`, which mines transaction metadata to produce
 *  OHLCV market-data for every order-book pair that had fills in that ledger.
 *  Registered in `Handler.cpp` as `Role::USER`, `Condition::NoCondition`.
 */

#include <xrpld/rpc/BookChanges.h>

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/ledger/ReadView.h>

#include <memory>

namespace xrpl {

/** Entry-point handler for the `book_changes` RPC command.
 *
 *  Resolves the ledger identified by the standard XRPL ledger-selector
 *  parameters (`ledger_hash`, `ledger_index`, or the shorthand strings
 *  `"validated"`, `"current"`, and `"closed"`) via `RPC::lookupLedger`.
 *  If no selector is supplied, the current (open) ledger is used by default.
 *
 *  The handler checks the resulting `shared_ptr` rather than inspecting the
 *  JSON error fields: a null pointer means the lookup failed and the partially-
 *  populated `result` already contains the appropriate error response. This is
 *  the idiomatic pattern used by all ledger-reading handlers in this subtree.
 *
 *  On success, delegates entirely to `RPC::computeBookChanges`, which iterates
 *  `sfAffectedNodes` across every transaction in the ledger to accumulate
 *  per-pair OHLCV statistics. See `BookChanges.h` for full algorithmic detail.
 *
 *  @param context  RPC dispatch context carrying request parameters, app
 *      references, and role information.
 *  @return On success, a JSON object containing `type`, `validated`,
 *      `ledger_index`, `ledger_hash`, `ledger_time`, and a `changes` array
 *      of per-pair OHLCV records (see `RPC::computeBookChanges`). On failure,
 *      the error JSON populated by `RPC::lookupLedger`.
 */
json::Value
doBookChanges(RPC::JsonContext& context)
{
    std::shared_ptr<ReadView const> ledger;

    json::Value result = RPC::lookupLedger(ledger, context);
    if (ledger == nullptr)
        return result;

    return RPC::computeBookChanges(ledger);
}

}  // namespace xrpl
