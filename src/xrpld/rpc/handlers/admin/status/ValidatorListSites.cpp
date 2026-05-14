/** @file
 *  Handler for the `validator_list_sites` admin RPC command.
 *
 *  Exposes the runtime state of the node's validator-list fetching subsystem
 *  (`ValidatorSite`) as a JSON response. The handler is registered with
 *  `Role::ADMIN` and no feature-flag condition, restricting it to trusted
 *  operators performing diagnostic or operational tasks.
 */

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ValidatorSite.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>

namespace xrpl {

/** Return the current state of all configured validator list sites.
 *
 *  Delegates entirely to `ValidatorSite::getJson()`, which serializes the
 *  per-site state maintained by the periodic HTTP fetch machinery: configured
 *  URIs, last-refresh timestamps, refresh intervals, redirect history, and
 *  the disposition of the last fetched list (accepted, rejected, or pending).
 *
 *  `ValidatorSite::getJson()` is thread-safe — it acquires `sites_mutex_`
 *  then `state_mutex_` internally (in that fixed order to prevent deadlock),
 *  so this handler may be called from any thread concurrently with the fetch
 *  timer without additional synchronisation.
 *
 *  @param context  The RPC dispatch context; `context.app` provides access to
 *      the application-singleton `ValidatorSite` subsystem.
 *  @return A JSON object whose structure is defined by
 *      `ValidatorSite::getJson()`.
 *  @note Registered in `Handler.cpp` as `{"validator_list_sites", ...,
 *      Role::ADMIN, Condition::NoCondition}`. No network-sync or ledger
 *      condition is required — the site list is available regardless of
 *      ledger state.
 */
json::Value
doValidatorListSites(RPC::JsonContext& context)
{
    return context.app.getValidatorSites().getJson();
}

}  // namespace xrpl
