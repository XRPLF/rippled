/** @file
 *  Handler for the `validators` admin RPC command.
 *
 *  Exposes the full state of the node's active Unique Node List (UNL) as a
 *  JSON response. The handler is registered with `Role::ADMIN` and no
 *  feature-flag condition, restricting it to authenticated operators and
 *  making it available regardless of ledger sync state.
 */

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>

namespace xrpl {

/** Return the full state of the node's active validator list.
 *
 *  Delegates entirely to `ValidatorList::getJson()`, which serializes the
 *  complete UNL state: trusted signing keys, quorum thresholds, publisher
 *  public keys, list sequence numbers, expiry timestamps, and per-publisher
 *  disposition flags (e.g. `accepted`, `expired`, `pending`, `stale`).
 *
 *  `ValidatorList::getJson()` is thread-safe and may be called concurrently
 *  with list updates. No locking is required by this handler.
 *
 *  @param context  The RPC dispatch context; `context.app` provides access
 *      to the application-singleton `ValidatorList` subsystem.
 *  @return A JSON object whose structure is defined by
 *      `ValidatorList::getJson()`.
 *  @note Registered in `Handler.cpp` as `{"validators", ..., Role::ADMIN,
 *      Condition::NoCondition}`. The handler carries no request parameters
 *      and performs no input validation — all content and error handling is
 *      the responsibility of `ValidatorList`. It responds even when the node
 *      has not yet synced, because UNL metadata is loaded at startup
 *      independent of ledger progress.
 *  @see doValidatorListSites (`ValidatorListSites.cpp`) for the sites from
 *      which this node fetches UNL updates.
 *  @see doValidatorInfo (`ValidatorInfo.cpp`) for the local node's own
 *      validator identity (ephemeral key, manifest, master key).
 *  @see doUnlList (`admin/UnlList.cpp`) for a narrower view that lists only
 *      trusted public keys without publisher-level metadata.
 */
json::Value
doValidators(RPC::JsonContext& context)
{
    return context.app.getValidators().getJson();
}

}  // namespace xrpl
