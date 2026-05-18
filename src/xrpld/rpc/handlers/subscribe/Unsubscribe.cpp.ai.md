# `Unsubscribe.cpp` — RPC Handler for Stream Unsubscription

## Role in the System

`Unsubscribe.cpp` implements `doUnsubscribe`, the RPC handler that tears down active push subscriptions in the XRPL server. It is the exact counterpart to `doSubscribe` in the same directory and delegates all actual state removal to `NetworkOPs` via the `context.netOps` accessor. The file lives in `src/xrpld/rpc/handlers/subscribe/` alongside `Subscribe.cpp`, forming the complete lifecycle management pair for the server's event-streaming subsystem.

## Subscriber Identity: Two Modes

The handler must first resolve *which* subscriber to remove subscriptions from. XRPL supports two subscription transport modes:

1. **WebSocket connections** — the connection object itself is a live `InfoSub` captured in `context.infoSub`. This is the common case for interactive clients.
2. **JSON-RPC with a `url` parameter** — an admin-configured outbound webhook that the server actively pushes to via `RPCSub`. The subscriber is looked up by URL string through `context.netOps.findRpcSub(strUrl)`.

The first guard at the top of the function enforces that exactly one of these two modes is available:

```cpp
if (!context.infoSub && !context.params.isMember(jss::url))
    return rpcError(rpcINVALID_PARAMS);
```

This prevents a footgun where a raw JSON-RPC call with no `url` and no active WebSocket connection would silently succeed or panic. The URL mode additionally requires `Role::ADMIN` — non-admin clients cannot reach a shared named subscriber. If `findRpcSub` returns null (the URL never had a subscription registered), the handler returns an empty success object rather than an error — unsubscribing from something that is already gone is treated as a no-op.

## Subscription Categories

After resolving `ispSub`, the handler walks through five independent subscription categories, each guarded by `isMember` checks so clients only need to include the categories they wish to remove:

**Named streams** — the `streams` array accepts any combination of: `server`, `ledger`, `manifests`, `transactions`, `transactions_proposed`, `validations`, `peer_status`, and `consensus`. All of these dispatch to `unsubServer`, `unsubLedger`, etc. on `NetworkOPs` by passing `ispSub->getSeq()` — a stable numeric identifier for the subscriber. Unknown stream names return `rpcSTREAM_MALFORMED` from the `else` branch. The legacy alias `rt_transactions` is accepted silently alongside `transactions_proposed`.

**Account (proposed) subscriptions** — `accounts_proposed` (legacy alias: `rt_accounts`) and `accounts` each accept an array of base58 account addresses. `RPC::parseAccountIds` converts them; an empty result means the addresses were malformed, returning `rpcACT_MALFORMED`. These call `unsubAccount(ispSub, ids, rt)` with the full `InfoSub::ref` rather than the sequence number, because this path needs to clean up state on both sides: the server's account-to-listener mapping and the `InfoSub` object's own subscription tracking.

**Account history stream** — the experimental `account_history_tx_stream` object allows a client to stop receiving historical transaction replay for an account. The optional `stop_history_tx_only` boolean lets a client halt the historical replay while keeping the live account subscription intact — the `historyOnly` flag flows straight through to `unsubAccountHistoryInternal` in `NetworkOPs`. This feature is marked experimental and its subscribe path warns clients accordingly.

**Order books** — the `books` array follows the same `taker_pays`/`taker_gets` structure as the subscribe path. `RPC::parseSubUnsubJson` resolves each leg to an `Asset`, and the pair forms a `Book`. The same-asset check (`book.in == book.out`) guards against degenerate markets. If the `both` or deprecated `both_sides` flag is set, the handler issues a second `unsubBook` call with the reversed book (`reversed(book)`), matching the symmetric subscribe behavior.

## Asymmetries Relative to `Subscribe.cpp`

The unsubscribe handler is deliberately simpler in several ways that reflect the semantics of removal vs. registration:

- `Subscribe.cpp` checks `isConsistent(book)` before adding an order book subscription; `Unsubscribe.cpp` does not. A client should be able to cleanly remove a subscription regardless of whether the book's internal state would pass consistency checks today.
- The `peer_status` stream requires `Role::ADMIN` to *subscribe* but no role check to *unsubscribe* — removing a subscription is always safe regardless of permission level.
- `book_changes` is a subscribable stream in `Subscribe.cpp` but conspicuously absent from `Unsubscribe.cpp`'s stream dispatch table. This means `book_changes` subscriptions cannot be unsubscribed via this handler — they are implicitly torn down when the connection closes.
- `Subscribe.cpp` calls `addRpcSub` to create the URL-based subscriber if absent; `Unsubscribe.cpp` returns an empty success instead of creating anything.

## URL-Cleanup Timing

The `removeUrl` boolean flag defers the call to `tryRemoveRpcSub` until after all subscription removal operations complete. This ordering matters: `tryRemoveRpcSub` checks whether the `RPCSub` still has active subscriptions before actually deleting the URL registration. Processing all individual `unsub*` calls first ensures the subscriber is truly empty before the cleanup check runs, preventing a race where the URL entry is removed while the subscriber still holds references.

## Error Handling

The handler returns early on the first error encountered within any category. Errors are returned as JSON using `rpcError()` with typed error codes from `ErrorCodes.h`: `rpcINVALID_PARAMS` for structural problems, `rpcSTREAM_MALFORMED` for unknown stream names or non-string stream elements, `rpcACT_MALFORMED` for unparseable account IDs, `rpcNO_PERMISSION` for the admin-only URL path, `rpcBAD_MARKET` for degenerate order books, and `rpcDOMAIN_MALFORMED` for malformed domain hex. A successful unsubscribe returns an empty JSON object — there is no acknowledgment payload.