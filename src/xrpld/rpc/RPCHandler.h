/** @file
 *  Public entry points for the XRPL RPC command dispatch layer.
 *
 *  This header is the only interface callers outside the RPC subsystem need
 *  in order to drive the full RPC pipeline: `doCommand` executes a request
 *  end-to-end, and `roleRequired` probes the handler table for the minimum
 *  permission level before any context is allocated.
 *
 *  All handler-table internals (`Handler.h`, `Tuning.h`, `Condition`) are
 *  confined to the `detail/` subdirectory and are not exposed here.
 *
 *  @see RPC::JsonContext, RPC::Role
 */

#pragma once

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Status.h>

namespace xrpl::RPC {

struct JsonContext;

/** Execute an RPC command and write the result into @p result.
 *
 *  Applies every gate in sequence before invoking the matched handler:
 *
 *  1. **Load shedding** — counts `jtCLIENT`-priority jobs; returns
 *     `rpcTOO_BUSY` if the queue exceeds `Tuning::kMAX_JOB_QUEUE_CLIENTS`.
 *     Callers holding `ADMIN` or `IDENTIFIED` roles bypass this check.
 *  2. **Command resolution** — extracts the name from the `command` or
 *     `method` JSON field (both are accepted for compatibility). If both are
 *     present with differing values the request is rejected as
 *     `rpcUNKNOWN_COMMAND`.
 *  3. **Role enforcement** — rejects non-admin callers attempting
 *     `Role::ADMIN`-gated commands with `rpcNO_PERMISSION`. This check
 *     occurs before any ledger-state inspection.
 *  4. **Network condition gates** — verifies amendment-blocked status,
 *     UNL-blocked status, `OperatingMode`, validated-ledger age, and
 *     current/validated ledger gap. Error codes are version-sensitive: API
 *     v1 returns `rpcNO_NETWORK` / `rpcNO_CURRENT` / `rpcNO_CLOSED`;
 *     API v2+ collapses these to `rpcNOT_SYNCED`.
 *  5. **Instrumented dispatch** — wraps the handler call with `PerfLog`
 *     start/finish/error events and a `JobQueue` load event for timing.
 *     Unhandled exceptions are caught and translated to `rpcINTERNAL`; if
 *     the load charge was still at `feeReferenceRPC` it is escalated to
 *     `feeExceptionRPC`.
 *
 *  @note `doCommand` is transport-agnostic: it fills only the inner
 *      `result` object. The surrounding envelope (`status` placement,
 *      `id`, `type` fields) is assembled by the HTTP/WebSocket server
 *      layers above this call.
 *
 *  @param context Fully-populated execution context including parsed params,
 *      resolved role, API version, and node subsystem references.
 *  @param result Output JSON object into which the handler writes its
 *      response fields. On error, an error object is injected before
 *      returning.
 *  @return A `Status` carrying the RPC error code, or `RpcSuccess` (0) on
 *      success.
 */
Status
doCommand(RPC::JsonContext& context, json::Value& result);

/** Look up the minimum `Role` required to invoke an RPC method.
 *
 *  Queries the static handler table without constructing a context or
 *  touching the job queue, allowing the transport layer to reject
 *  unauthorized requests early — before any work is queued. `doCommand`
 *  re-validates the role internally, so this function acts as a fast
 *  pre-authorization probe only.
 *
 *  @param version     Client's requested API version, used to select the
 *      correct version-ranged handler entry.
 *  @param betaEnabled Whether beta API versions are enabled on this node
 *      (`[beta_rpc_api]` config flag).
 *  @param method      The RPC method name to look up (e.g., `"ledger"`,
 *      `"account_info"`).
 *  @return The `Role` the caller must hold to execute `method`, or
 *      `Role::FORBID` if the method does not exist in the handler table
 *      for the given version. The `FORBID` sentinel signals the server
 *      layer to close the connection rather than queue the request.
 */
Role
roleRequired(unsigned int version, bool betaEnabled, std::string const& method);

}  // namespace xrpl::RPC
