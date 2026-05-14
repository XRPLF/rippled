/** @file
 *  RPC handler for the `tx_reduce_relay` command.
 *
 *  Provides a read-only introspection endpoint that surfaces transaction relay
 *  performance metrics collected by the node's peer overlay network.  The
 *  handler is registered with `Role::USER` and `NO_CONDITION`, so any connected
 *  client may query it without special privileges or an active network
 *  connection requirement.
 */

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Overlay.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>

namespace xrpl {

/** Return a rolling-average snapshot of transaction reduce-relay metrics.
 *
 *  Delegates unconditionally to `Overlay::txMetrics()`, which serializes the
 *  `metrics::TxMetrics` struct maintained by `OverlayImpl` into a JSON object.
 *  There is no input parsing, no parameter validation, and no transformation of
 *  the result — the `Json::Value` produced by the overlay is returned directly
 *  to the caller.
 *
 *  The returned object contains thirteen `jss::txr_*`-keyed fields covering:
 *  - Per-second rolling averages (message count and byte size) for the five
 *    tracked protocol message types: `TMTransaction`, `TMHaveTransactions`,
 *    `TMGetLedger`, `TMLedgerData`, and `TMTransactions`.
 *  - Per-transaction sample averages for selected, suppressed, and
 *    feature-disabled peers — the three-way partition produced by the relay
 *    algorithm each time a transaction is forwarded.
 *  - A per-second rate of transactions received in `TMTransactions` bundles
 *    that the local node was missing, indicating reactive-request load induced
 *    by relay suppression.
 *
 *  All values reflect a trailing ~30-second exponential window.  Fields with
 *  no recent activity report `"0"`.  Values are serialized as JSON strings
 *  rather than JSON integers; callers must parse them for numeric comparison.
 *
 *  Thread safety and data correctness are the responsibility of `TxMetrics`
 *  (which guards its state with an internal `mutex`) and `OverlayImpl` (which
 *  additionally serializes writes via its Boost.Asio strand).
 *
 *  @param context Standard RPC dispatch context; only `context.app` is used to
 *      reach the overlay via `getOverlay()`.
 *  @return A `Json::Value` object containing the current reduce-relay metric
 *      snapshot, as produced by `Overlay::txMetrics()`.
 */
json::Value
doTxReduceRelay(RPC::JsonContext& context)
{
    return context.app.getOverlay().txMetrics();
}

}  // namespace xrpl
