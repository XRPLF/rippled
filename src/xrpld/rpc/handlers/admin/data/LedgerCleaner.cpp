/** @file
 *  RPC handler for the `ledger_cleaner` admin command.
 *
 *  Bridges the JSON-RPC layer to the background `LedgerCleaner` subsystem,
 *  which audits and repairs ledger and transaction database continuity on a
 *  running node. Registered as `Role::ADMIN` with
 *  `Condition::NeedsNetworkConnection`; unreachable from unprivileged peers.
 */

#include <xrpld/app/ledger/LedgerCleaner.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/json/json_value.h>

namespace xrpl {

/** Configure the background ledger-cleaner and schedule a cleaning pass.
 *
 *  Forwards `context.params` unchanged to `LedgerCleaner::clean()`, which
 *  schedules the requested audit-and-repair work on an implementation-defined
 *  internal thread and returns immediately. This handler therefore returns
 *  before any cleaning work has occurred.
 *
 *  All parameter interpretation (ledger sequence range, whether to fix
 *  transaction entries, etc.) is delegated entirely to the concrete
 *  `LedgerCleaner` implementation; this handler performs no pre-validation.
 *  Runtime progress can be observed through node logs or the
 *  `beast::PropertyStream` interface exposed by `LedgerCleaner`.
 *
 *  @param context  JSON-RPC dispatch context; `context.params` is passed
 *      verbatim to `LedgerCleaner::clean()`.
 *  @return A JSON object containing the static confirmation string
 *      `"Cleaner configured"`. The cleaner may not have started or finished
 *      by the time this value is returned to the caller.
 */
json::Value
doLedgerCleaner(RPC::JsonContext& context)
{
    context.app.getLedgerCleaner().clean(context.params);
    return RPC::makeObjectValue("Cleaner configured");
}

}  // namespace xrpl
