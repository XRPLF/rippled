/** @file
 *  Declares the RPC handler registry and dispatch primitives.
 *
 *  Defines `Handler` (a single dispatchable RPC endpoint), the `Condition`
 *  bitmask encoding node-state requirements, the `conditionMet()` gating
 *  predicate, and the `getHandler()` / `getHandlerNames()` lookup interface.
 *  The backing `HandlerTable` singleton lives in the companion `Handler.cpp`
 *  translation unit, keeping handler-table compile-time dependencies out of
 *  every translation unit that only needs to call `getHandler()`.
 */
#pragma once

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/RPCHandler.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/server/NetworkOPs.h>

namespace json {
class Object;
}  // namespace json

namespace xrpl::RPC {

/** Bitmask of node-state prerequisites that must be satisfied before an RPC
 *  method may be dispatched.
 *
 *  Each enumerator represents an independent requirement. Values are powers
 *  of two so they can be OR-combined, though the current handler table only
 *  uses one condition per handler. `NoCondition` methods (e.g. `sign`,
 *  `random`, `channel_authorize`) execute even on a fully isolated node.
 */
enum class Condition {
    /** No network or ledger state is required; always executable. */
    NoCondition = 0,
    /** The node must be at least `SYNCING` (connected to the network). */
    NeedsNetworkConnection = 1,
    /** A current open ledger must be available; implies network connection. */
    NeedsCurrentLedger = 1 << 1,
    /** A recently closed ledger must be available via `LedgerMaster`. */
    NeedsClosedLedger = 1 << 2,
};

/** Descriptor for a single dispatchable RPC endpoint.
 *
 *  The `HandlerTable` singleton in `Handler.cpp` stores one `Handler` per
 *  registered method (keyed by `name`). Multiple `Handler` entries may share
 *  the same `name` as long as their `[minApiVer, maxApiVer]` ranges do not
 *  overlap — version-range overlap is a fatal `LogicError` detected at
 *  startup.
 *
 *  `valueMethod` writes its output into a `json::Value&` reference rather
 *  than returning by value, avoiding an extra copy of potentially large JSON
 *  results while keeping `Status` independent of the output.
 */
struct Handler
{
    /** Callable type for a single RPC endpoint.
     *
     *  @tparam JsonValue  The JSON output type (typically `json::Value`).
     *
     *  Receives the dispatch context and an output reference to populate.
     *  Returns `Status()` on success or a non-zero `Status` on failure.
     */
    template <class JsonValue>
    using Method = std::function<Status(JsonContext&, JsonValue&)>;

    /** JSON-RPC method name (e.g. `"account_info"`). Interned string literal. */
    char const* name;

    /** Callable that executes the handler and writes the JSON result. */
    Method<json::Value> valueMethod;

    /** Minimum role (`USER` or `ADMIN`) the caller must hold. */
    Role role;

    /** Node-state prerequisites checked by `conditionMet()` before dispatch. */
    RPC::Condition condition;

    /** Lowest API version that routes to this handler entry (inclusive). */
    unsigned minApiVer = kAPI_MINIMUM_SUPPORTED_VERSION;

    /** Highest API version that routes to this handler entry (inclusive). */
    unsigned maxApiVer = kAPI_MAXIMUM_VALID_VERSION;
};

/** Look up the handler for an incoming RPC request.
 *
 *  Delegates to the process-wide `HandlerTable` singleton. The returned
 *  pointer is into the immutable singleton table and is valid for the
 *  lifetime of the process; do not delete or cache it across restarts.
 *
 *  Returns `nullptr` when `version` is below `kAPI_MINIMUM_SUPPORTED_VERSION`,
 *  above `kAPI_MAXIMUM_SUPPORTED_VERSION` (or `kAPI_BETA_VERSION` when
 *  `betaEnabled` is true), or when no handler is registered for `name` at
 *  that version.
 *
 *  @param version      API version of the incoming request.
 *  @param betaEnabled  Whether to extend the upper version bound to
 *      `kAPI_BETA_VERSION` for this node.
 *  @param name         The RPC method name string (e.g. `"account_info"`).
 *  @return Pointer to the matching `Handler`, or `nullptr` if not found.
 */
Handler const*
getHandler(unsigned int version, bool betaEnabled, std::string const&);

/** Construct a `json::Value` object with a single named field.
 *
 *  Creates a `json::ValueType::Object` and assigns `value` to `field`.
 *  Used as a defensive fallback in the old-style handler adapter (`byRef()`)
 *  when a legacy handler returns a non-object JSON value — a programming
 *  error that should never occur in a correct handler.
 *
 *  @tparam Value  Any type assignable to a `json::Value` field.
 *  @param value   The value to store.
 *  @param field   The key to store it under; defaults to `jss::message`.
 *  @return A `json::Value` object with one entry: `{ field: value }`.
 */
template <class Value>
json::Value
makeObjectValue(Value const& value, json::StaticString const& field = jss::message)
{
    json::Value result(json::ValueType::Object);
    result[field] = value;
    return result;
}

/** Return the names of all registered RPC methods.
 *
 *  Used by introspection paths and tests to enumerate the full method surface.
 *  The returned `char const*` pointers are interned into the immutable
 *  singleton table; they are valid for the process lifetime and must not be
 *  freed or compared by address.
 *
 *  @return A `std::set<char const*>` containing one entry per unique method name.
 */
std::set<char const*>
getHandlerNames();

/** Check whether the required node-state condition is currently satisfied.
 *
 *  This is the single enforcement point for the `Condition` contract. Checks
 *  are performed in order; the first failure terminates the check and returns
 *  the appropriate error code. The checks are:
 *
 *  1. **Amendment block** — if the node is amendment-blocked and
 *     `conditionRequired != NoCondition`, returns `RpcAmendmentBlocked`. Reads
 *     from an amendment-blocked node would surface ledger state that diverges
 *     from the rest of the network.
 *  2. **UNL block** — if the validator list has expired and
 *     `conditionRequired != NoCondition`, returns `RpcExpiredValidatorList`.
 *  3. **Operating mode** — the node must be at least `SYNCING`. On failure,
 *     returns `RpcNoNetwork` (API v1) or `RpcNotSynced` (API v2+).
 *  4. **Ledger freshness** (networked nodes only) — the last validated ledger
 *     must be younger than `Tuning::kMAX_VALIDATED_LEDGER_AGE` (2 minutes),
 *     and the current ledger index must be within 10 of the validated index.
 *     On failure, returns `RpcNoCurrent` (v1) or `RpcNotSynced` (v2+).
 *  5. **Closed ledger** — if `conditionRequired != NoCondition` and
 *     `LedgerMaster::getClosedLedger()` returns null, returns `RpcNoClosed`
 *     (v1) or `RpcNotSynced` (v2+).
 *
 *  The API v1 / v2+ error split is intentional: v2 collapses `RpcNoNetwork`,
 *  `RpcNoCurrent`, and `RpcNoClosed` into the single `RpcNotSynced` for a
 *  cleaner client experience.
 *
 *  @tparam T  A context type that exposes `app`, `netOps`, `ledgerMaster`,
 *      `j`, and `apiVersion` — typically `JsonContext` or a gRPC context.
 *  @param conditionRequired  The bitmask condition declared by the handler.
 *  @param context            The RPC dispatch context for the current request.
 *  @return `RpcSuccess` if all required conditions are met; otherwise the
 *      appropriate `ErrorCodeI` for the first failing check.
 *
 *  @note Standalone mode (`app.config().standalone()`) bypasses the ledger
 *      freshness check (step 4) entirely.
 */
template <class T>
ErrorCodeI
conditionMet(Condition conditionRequired, T& context)
{
    if (context.app.getOPs().isAmendmentBlocked() && (conditionRequired != Condition::NoCondition))
    {
        return RpcAmendmentBlocked;
    }

    if (context.app.getOPs().isUNLBlocked() && (conditionRequired != Condition::NoCondition))
    {
        return RpcExpiredValidatorList;
    }

    if ((conditionRequired != Condition::NoCondition) &&
        (context.netOps.getOperatingMode() < OperatingMode::SYNCING))
    {
        JLOG(context.j.info()) << "Insufficient network mode for RPC: "
                               << context.netOps.strOperatingMode();

        if (context.apiVersion == 1)
            return RpcNoNetwork;
        return RpcNotSynced;
    }

    if (!context.app.config().standalone() && conditionRequired != Condition::NoCondition)
    {
        if (context.ledgerMaster.getValidatedLedgerAge() > Tuning::kMAX_VALIDATED_LEDGER_AGE)
        {
            if (context.apiVersion == 1)
                return RpcNoCurrent;
            return RpcNotSynced;
        }

        auto const cID = context.ledgerMaster.getCurrentLedgerIndex();
        auto const vID = context.ledgerMaster.getValidLedgerIndex();

        if (cID + 10 < vID)
        {
            JLOG(context.j.debug()) << "Current ledger ID(" << cID
                                    << ") is less than validated ledger ID(" << vID << ")";
            if (context.apiVersion == 1)
                return RpcNoCurrent;
            return RpcNotSynced;
        }
    }

    if ((conditionRequired != Condition::NoCondition) && !context.ledgerMaster.getClosedLedger())
    {
        if (context.apiVersion == 1)
            return RpcNoClosed;
        return RpcNotSynced;
    }

    return RpcSuccess;
}

}  // namespace xrpl::RPC
