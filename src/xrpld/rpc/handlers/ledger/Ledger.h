/** @file
 *  Declares `LedgerHandler`, the new-style class-based RPC handler for the
 *  `ledger` JSON-RPC command.
 *
 *  `LedgerHandler` is one of only two class-based handlers in the codebase
 *  (the other being `VersionHandler`). All other handlers are registered as
 *  bare function pointers in the legacy `handlerArray` table; this handler is
 *  added via `addHandler<LedgerHandler>()` during `HandlerTable` construction.
 */
#pragma once

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/jss.h>

namespace json {
class Object;
}  // namespace json

namespace xrpl::RPC {

struct JsonContext;

/** Two-phase handler for the `ledger` JSON-RPC command.
 *
 *  Implements the primary client endpoint for inspecting ledger state,
 *  transactions, and the transaction queue. The handler follows a two-phase
 *  protocol: `check()` validates parameters and resolves the target ledger,
 *  then `writeResult()` serialises the response.
 *
 *  The static metadata members (`name`, `role`, `condition`, `minApiVer`,
 *  `maxApiVer`) are read by `addHandler<T>()` at registration time and
 *  validated via `static_assert`. `role = Role::USER` makes the command
 *  available to all authenticated callers without admin privilege.
 *  `condition = NoCondition` allows the handler to run regardless of network
 *  synchronisation state, so it can always return whatever ledger state is
 *  locally available.
 *
 *  @see Handler.cpp `HandlerTable` for registration via `addHandler<T>()`.
 *  @see Ledger.cpp `doLedgerGrpc` for the parallel gRPC surface, which shares
 *      no code path but lives in the same translation unit.
 */
class LedgerHandler
{
public:
    /** Construct a handler bound to the given request context.
     *
     *  @param context  The active JSON-RPC request context. Held by reference
     *      for the lifetime of this handler; the caller must ensure the context
     *      outlives the handler.
     */
    explicit LedgerHandler(JsonContext& context);

    /** Validate request parameters, resolve the target ledger, and build the
     *  options bitmask for `writeResult()`.
     *
     *  Parses all boolean flags (`full`, `transactions`, `accounts`, `expand`,
     *  `binary`, `owner_funds`, `queue`) and OR-combines them into `options_`.
     *  If none of `ledger`, `ledger_hash`, or `ledger_index` are present in
     *  the request, the method returns `Status::kOK` without populating
     *  `ledger_`, which causes `writeResult()` to emit the dual
     *  closed-and-open response instead.
     *
     *  When a ledger identifier is supplied, `lookupLedger` resolves and
     *  stores `ledger_` and seeds `result_` with preliminary metadata that
     *  `writeResult()` will merge into the final output.
     *
     *  @return `Status::kOK` on success; `rpcINVALID_PARAMS` if any boolean
     *      flag is not actually a boolean, or if `queue=true` is requested
     *      against a non-open ledger; `rpcNO_PERMISSION` if `full` or
     *      `accounts` is requested by a non-admin (non-unlimited) caller;
     *      `rpcTOO_BUSY` if the node is under heavy local load and a full or
     *      accounts dump is requested.
     *
     *  @note Dumping the entire account state trie (`full` or `accounts`) is
     *      restricted to admin/unlimited roles and further throttled by local
     *      load. Binary mode charges `feeMediumBurdenRPC`; JSON mode charges
     *      `feeHeavyBurdenRPC`. The `queue` flag is only meaningful against
     *      the open ledger; supplying it with a closed or historical ledger
     *      selector returns `rpcINVALID_PARAMS`.
     */
    Status
    check();

    /** Serialize the resolved ledger into the JSON output value.
     *
     *  If `ledger_` was populated by `check()`, merges the diagnostic metadata
     *  accumulated in `result_` and calls `addJson()` with the `options_`
     *  bitmask and any captured transaction-queue snapshot. If `ledger_` is
     *  not set (no ledger selector was given), emits both the current closed
     *  and current open ledger under the sibling keys `closed` and `open`.
     *
     *  Appends a structured `warnings` array entry when the deprecated `type`
     *  filter field is detected in the request parameters.
     *
     *  @param value  Output JSON object that receives the ledger description.
     */
    void
    writeResult(json::Value& value);

    // NOLINTBEGIN(readability-identifier-naming)
    /** RPC method name used as the dispatch key in the handler table. */
    static constexpr char name[] = "ledger";

    /** Minimum API version that this handler accepts. */
    static constexpr unsigned minApiVer = RPC::kAPI_MINIMUM_SUPPORTED_VERSION;

    /** Maximum API version that this handler accepts. */
    static constexpr unsigned maxApiVer = RPC::kAPI_MAXIMUM_VALID_VERSION;

    /** Access role required; USER means no admin privilege is needed. */
    static constexpr Role role = Role::USER;

    /** Node-state prerequisite; NoCondition allows dispatch during initial sync. */
    static constexpr Condition condition = Condition::NoCondition;
    // NOLINTEND(readability-identifier-naming)

private:
    /** Active request context providing params, app, role, and API version. */
    JsonContext& context_;

    /** Resolved ledger, populated by `check()` when a ledger selector is given.
     *  Null when no selector is present; `writeResult()` uses this to choose
     *  between the single-ledger and dual closed+open response paths.
     */
    std::shared_ptr<ReadView const> ledger_;

    /** Transaction-queue snapshot fetched by `check()` when `queue=true`.
     *  Only populated if `ledger_` refers to the open ledger.
     */
    std::vector<TxQ::TxDetails> queueTxs_;

    /** Intermediate JSON seeded by `lookupLedger()` during `check()`;
     *  merged into the final output by `writeResult()` via `copyFrom()`.
     */
    json::Value result_;

    /** Bitmask of `LedgerFill::Options` flags built from request parameters
     *  during `check()` and passed to `addJson()` in `writeResult()`.
     */
    int options_ = 0;
};

}  // namespace xrpl::RPC
