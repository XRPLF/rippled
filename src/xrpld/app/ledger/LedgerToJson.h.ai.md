# `LedgerToJson.h` — Ledger Serialization Interface

This header defines the public interface for serializing XRP Ledger data into JSON, a capability exercised by nearly every ledger-related RPC endpoint. It lives in `src/xrpld/app/ledger/` and is the single include callers need to convert a `ReadView` into the JSON structure returned to API clients. The implementation is in `detail/LedgerToJson.cpp`.

## The `LedgerFill` Parameter Object

Rather than a long argument list, the header introduces `LedgerFill` as a self-contained descriptor that bundles the ledger, serialization options, and contextual services. This is deliberate: `addJson()` and `getJson()` need several independent pieces of information, and packing them here keeps the call sites readable — notably in `LedgerHandler::writeResult()`:

```cpp
addJson(value, {*ledger_, &context_, options_, queueTxs_});
```

`LedgerFill` holds the ledger as a `ReadView const&` (not a pointer or shared_ptr), so the caller's lifetime guarantee is structural. The `txQueue` is taken by value with `std::move`, reflecting that the queue snapshot is a one-time capture from `TxQ::getTxs()` whose ownership should transfer into the fill descriptor.

The most subtle field is the `RPC::Context const*`. It is nullable by design — `getJson()` is sometimes called without a live RPC context (for example in `doLedgerData` when generating the base ledger header). When non-null, `context` contributes three things: the API version that governs output shape, the `LedgerMaster` reference needed to resolve validation status, and the journal for error logging.

The constructor resolves `closeTime` eagerly:

```cpp
if (context)
    closeTime = context->ledgerMaster.getCloseTimeBySeq(ledger.seq());
```

This is a deliberate optimization. `getCloseTimeBySeq()` may involve a database or in-memory lookup that is expensive to repeat per-transaction. By caching it in `std::optional<NetClock::time_point>` at construction, the value is computed once and then stamped onto every expanded transaction's `close_time_iso` field in the implementation.

## The Options Bitmask

The nested `Options` enum provides seven orthogonal flags combined via bitwise OR at call sites:

| Flag | Value | Effect |
|------|-------|--------|
| `dumpTxrp` | 1 | Include the transactions array |
| `dumpState` | 2 | Include the account state trie |
| `expand` | 4 | Serialize objects as full JSON instead of hash strings |
| `full` | 8 | Implies both `dumpTxrp` and `dumpState`; also forces expand |
| `binary` | 16 | Hex-encode serialized blobs instead of JSON fields |
| `ownerFunds` | 32 | Annotate `OfferCreate` transactions with the creator's available balance |
| `dumpQueue` | 64 | Append pending transaction queue contents |

The `full` flag is a convenience superset: `isExpanded()` in the implementation returns true when `full` is set even if `expand` is not, avoiding redundancy in call sites. Callers that set `full` or `dumpState` are required by `LedgerHandler` to hold unlimited permissions and pass a load check before constructing the fill — the serialization layer itself has no access control, keeping concerns separated.

## `addJson()` vs `getJson()`

These two functions expose the same underlying serialization pipeline with different placement semantics.

`addJson(json, fill)` nests the ledger under `json["ledger"]` — the JSON envelope shape expected by the `ledger` RPC method. Queue data, if requested via `dumpQueue`, is placed *outside* that nested object, directly in `json["queue_data"]`. This asymmetry is intentional: the transaction queue is metadata *about* the open ledger rather than data encoded in the ledger itself, so it sits at the response's top level.

`getJson(fill)` returns a fresh `Json::Value` containing only the ledger's own fields, without the `"ledger"` wrapper. This is used in `doLedgerData` when returning the ledger header on the first page of a paginated state-node query — there the caller places the result under `jvResult[jss::ledger]` itself.

## API Version Awareness

The `context->apiVersion` value, passed through `LedgerFill`, controls several output differences in the implementation. In API v1, `ledger_index` is a string; in v2 it is a native integer. Expanded transactions in v2 move transaction fields under a `tx_json` sub-object and use `meta_blob` rather than `meta` for binary metadata. The hash field is added explicitly in v2. Without a context, the implementation defaults to `apiMaximumSupportedVersion`, so callers that omit context receive the current canonical format.

## `copyFrom()`

This utility function merges key-value pairs from one `Json::Value` object into another. Its short-circuit — assigning directly when the destination is null — handles the common case where a field is being set for the first time. When the destination already has content, it iterates `getMemberNames()` and copies each key individually (a shallow merge, not a recursive deep merge). An assertion guards that the source is an object or null; callers must not pass arrays or scalar values. The TODO comment in the implementation acknowledges that the deep-copy semantics of the fallback path may deserve reconsideration if JSON values ever contain nested shared references.

## Exception Safety

The transaction-iteration path in the implementation is wrapped in a try/catch. If any transaction in the ledger's storage is undeserializable — for instance due to a corrupt or incompatible encoding — the exception is caught, logged to the context's journal at error level, and the response is returned with whatever transactions were successfully serialized before the failure. This prevents a single bad ledger entry from producing an empty or server-error response.