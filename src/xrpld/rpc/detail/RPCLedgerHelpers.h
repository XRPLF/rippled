/** @file
 *  Ledger resolution utilities for the RPC layer.
 *
 *  Declares every function used by RPC handlers to translate a caller-supplied
 *  ledger descriptor — hash, sequence number, shortcut string, or gRPC
 *  `LedgerSpecifier` — into a concrete `std::shared_ptr<ReadView const>`.
 *  The implementations enforce staleness guards and API-version–aware error
 *  codes so that every handler gets consistent behaviour for free.
 *
 *  @see lookupLedger, getLedger, getOrAcquireLedger
 */

#pragma once

#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/ledger/Ledger.h>
#include <xrpl/proto/org/xrpl/rpc/v1/xrp_ledger.pb.h>
#include <xrpl/protocol/LedgerShortcut.h>
#include <xrpl/server/NetworkOPs.h>

#include <optional>

namespace xrpl {

class ReadView;
class Transaction;

namespace RPC {

struct JsonContext;

/** Retrieve a ledger by its 256-bit hash.
 *
 *  Delegates directly to `LedgerMaster::getLedgerByHash()`. No staleness
 *  check is applied: the caller identified an immutable object by identity,
 *  so the concept of "too old" does not apply.
 *
 *  @tparam T Type of the ledger output (typically `std::shared_ptr<ReadView const>`).
 *  @param ledger Output; set to the resolved ledger on success.
 *  @param ledgerHash The 256-bit hash of the desired ledger.
 *  @param context RPC context providing `LedgerMaster` and application config.
 *  @return `Status::kOK` if the ledger is in cache; `rpcLGR_NOT_FOUND` otherwise.
 */
template <class T>
Status
getLedger(T& ledger, uint256 const& ledgerHash, Context const& context);

/** Retrieve a ledger by sequence number with a staleness guard.
 *
 *  First tries `LedgerMaster::getLedgerBySeq()`; if that misses, falls back
 *  to the current open ledger when its sequence matches the requested index
 *  (the open ledger has not yet been persisted to the sequence index). After
 *  a successful lookup, if the ledger's sequence is ahead of the last
 *  validated index *and* the node's validated ledger is more than two minutes
 *  old (`Tuning::kMAX_VALIDATED_LEDGER_AGE`), the ledger is rejected with a
 *  sync error to prevent a drifted node from serving data it cannot vouch for.
 *
 *  @tparam T Type of the ledger output (typically `std::shared_ptr<ReadView const>`).
 *  @param ledger Output; set on success, reset on sync error.
 *  @param ledgerIndex Sequence number of the desired ledger.
 *  @param context RPC context providing `LedgerMaster` and application config.
 *  @return `Status::kOK` on success; `rpcLGR_NOT_FOUND` if the ledger is not
 *      cached; `rpcNO_NETWORK` (API v1) or `rpcNOT_SYNCED` (API v2+) if the
 *      ledger sequence is ahead of the validated chain and the node is stale.
 */
template <class T>
Status
getLedger(T& ledger, uint32_t ledgerIndex, Context const& context);

/** Retrieve a ledger by shortcut (`Current`, `Closed`, or `Validated`).
 *
 *  Checks for staleness *before* fetching: a node whose last validated ledger
 *  is older than `Tuning::kMAX_VALIDATED_LEDGER_AGE` always returns a sync
 *  error, regardless of which shortcut was requested. For `Current` and
 *  `Closed`, an additional guard rejects results when the shortcut ledger is
 *  more than 10 sequences behind the last validated index, protecting callers
 *  from data on a clearly-behind node.
 *
 *  @tparam T Type of the ledger output (typically `std::shared_ptr<ReadView const>`).
 *  @param ledger Output; set on success, reset on error.
 *  @param shortcut Which ledger to retrieve: `Current`, `Closed`, or `Validated`.
 *  @param context RPC context providing `LedgerMaster` and application config.
 *  @return `Status::kOK` on success; `rpcNO_NETWORK` (API v1) or `rpcNOT_SYNCED`
 *      (API v2+) if the node is stale or the sequence gap exceeds 10;
 *      `rpcINVALID_PARAMS` for an unrecognised shortcut value.
 *  @note `Current` must return an open ledger; `Closed` and `Validated` must
 *      return closed ledgers — violations trigger `XRPL_ASSERT` failures.
 *  @note The sequence-gap threshold of 10 is a hard-coded heuristic.
 */
template <class T>
Status
getLedger(T& ledger, LedgerShortcut shortcut, Context const& context);

/** Resolve a ledger from a JSON-RPC request and return a result object.
 *
 *  Convenience wrapper around the three-argument overload. On success,
 *  the returned `Json::Value` contains ledger identifying fields (see the
 *  three-argument overload). On failure, the `Status` is injected into the
 *  returned object via `Status::inject()`, giving callers a single value to
 *  check and forward.
 *
 *  @param ledger Output; set on success, unmodified on error.
 *  @param context JSON-RPC context whose `params` supply the ledger selector.
 *  @return A `Json::Value` containing ledger identifiers on success, or an
 *      error description with the injected status on failure.
 *  @see lookupLedger(std::shared_ptr<ReadView const>&, JsonContext const&, Json::Value&)
 */
Json::Value
lookupLedger(std::shared_ptr<ReadView const>&, JsonContext const&);

/** Resolve a ledger from a JSON-RPC request and populate an existing result object.
 *
 *  Parses the request `params` for exactly one of `ledger_hash`,
 *  `ledger_index`, or the deprecated `ledger` field and resolves the
 *  corresponding ledger. On success, writes identifying fields into `result`:
 *  closed ledgers receive `ledger_hash` and `ledger_index`; the open (current)
 *  ledger receives only `ledger_current_index`. The `validated` boolean is
 *  always written. Defaults to the current open ledger when no selector is
 *  present in `params`.
 *
 *  @param ledger Output; set on success.
 *  @param context JSON-RPC context whose `params` supply the ledger selector.
 *  @param result JSON object to populate with ledger identifiers on success;
 *      not modified on failure.
 *  @return `Status::kOK` on success; `rpcINVALID_PARAMS` for malformed or
 *      conflicting selector fields; network/sync errors from `getLedger()`.
 *  @note The deprecated `ledger` field is accepted but intentionally omitted
 *      from the error message when `ledger_hash` and `ledger_index` conflict,
 *      to discourage its use in new clients.
 */
Status
lookupLedger(std::shared_ptr<ReadView const>&, JsonContext const&, Json::Value& result);

/** Resolve a ledger from a gRPC request context.
 *
 *  Extracts the `LedgerSpecifier` from the gRPC request and delegates to
 *  `ledgerFromSpecifier()`. Explicitly instantiated for
 *  `GetLedgerEntryRequest`, `GetLedgerDataRequest`, and `GetLedgerRequest`.
 *
 *  @tparam T Type of the ledger output (typically `std::shared_ptr<ReadView const>`).
 *  @tparam R gRPC request type; must expose a `.ledger()` method returning
 *      a `LedgerSpecifier`.
 *  @param ledger Output; set on success.
 *  @param context gRPC context whose `params` hold the request object.
 *  @return `Status::kOK` on success; errors propagated from `ledgerFromSpecifier()`.
 */
template <class T, class R>
Status
ledgerFromRequest(T& ledger, GRPCContext<R> const& context);

/** Resolve a ledger from a protobuf `LedgerSpecifier`.
 *
 *  Switches on the specifier's `ledger_case()` and fans out to the
 *  appropriate `getLedger()` overload: `kHash` → hash lookup, `kSequence` →
 *  sequence lookup, `kShortcut` / `LEDGER_NOT_SET` → maps
 *  `SHORTCUT_VALIDATED`, `SHORTCUT_CURRENT`, `SHORTCUT_UNSPECIFIED`, and
 *  `SHORTCUT_CLOSED` to their `LedgerShortcut` equivalents.
 *
 *  @tparam T Type of the ledger output (typically `std::shared_ptr<ReadView const>`).
 *  @param ledger Output; reset on entry, set on success.
 *  @param specifier Protobuf specifier describing the desired ledger.
 *  @param context RPC context providing `LedgerMaster` and application config.
 *  @return `Status::kOK` on success; `rpcINVALID_PARAMS` if the hash bytes are
 *      malformed; other errors propagated from `getLedger()`.
 *  @note An unrecognised shortcut value leaves `ledger` reset and returns
 *      `Status::kOK` — callers should validate the specifier before calling.
 */
template <class T>
Status
ledgerFromSpecifier(
    T& ledger,
    org::xrpl::rpc::v1::LedgerSpecifier const& specifier,
    Context const& context);

/** Retrieve a ledger by hash or index, triggering a network fetch if needed.
 *
 *  Unlike `getLedger()`, this function can initiate an active acquisition
 *  via `InboundLedgers::acquire()` when the ledger is not locally cached.
 *  It is intended exclusively for the `ledger_request` admin command.
 *
 *  Exactly one of `ledger_hash` (hex string) or `ledger_index` (positive
 *  integer) must be supplied; no shortcut strings and no deprecated `ledger`
 *  field are accepted. For index-based lookup, the function walks the skip
 *  list on the validated ledger via `hashOfSeq()`; if the skip list does not
 *  reach far enough back, it locates a boundary candidate via
 *  `getCandidateLedger()`, acquires that intermediate ledger, and then
 *  resolves the final hash. In standalone mode, the `InboundLedgers` path is
 *  bypassed and only the local cache is consulted.
 *
 *  When an acquisition is still in progress, the returned error `Json::Value`
 *  contains an `"acquiring"` field with the in-progress acquisition status —
 *  this is a polling model; callers should retry until the ledger is available.
 *
 *  @param context JSON-RPC context; must contain exactly one of `ledger_hash`
 *      or `ledger_index` in `params`.
 *  @return On success, a `shared_ptr<Ledger const>` for the resolved ledger.
 *      On failure, a `Json::Value` describing the error; may include an
 *      `"acquiring"` field when a network fetch is pending.
 *  @note The index path validates that the node's ledger is not stale
 *      (> `Tuning::kMAX_VALIDATED_LEDGER_AGE`) before attempting resolution.
 *  @note API v1 callers receive `rpcNO_CURRENT` on staleness; v2+ callers
 *      receive `rpcNOT_SYNCED`.
 */
Expected<std::shared_ptr<Ledger const>, Json::Value>
getOrAcquireLedger(RPC::JsonContext const& context);

}  // namespace RPC

}  // namespace xrpl
