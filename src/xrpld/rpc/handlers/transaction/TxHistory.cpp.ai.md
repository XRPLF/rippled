# `TxHistory.cpp` — Paginated Transaction History RPC Handler

This file implements the `tx_history` RPC command (`doTxHistory`), a single-function handler that exposes a simple paginated interface for browsing the server's local transaction history stored in its relational database. It acts as a thin bridge between the RPC dispatch layer and the `RelationalDatabase` abstraction, letting clients step through historical transactions by offset rather than by ledger hash or transaction ID.

## Role in the System

Unlike sibling handlers such as `Tx.cpp` or `TransactionEntry.cpp` — which look up specific transactions by identifier — `doTxHistory` serves bulk browsing use cases. It returns a sequential slice of the transaction log starting at a caller-supplied offset, making it most useful for debugging, monitoring, or bulk export scenarios rather than precise lookups. The `RelationalDatabase::getTxHistory()` interface specifies that it returns the 20 most recent transactions starting from `startIndex`, sorted in descending ledger sequence order; the handler itself imposes no additional page-size logic and trusts the database layer to enforce that bound.

The endpoint is gated immediately by `context.app.config().useTxTables()`. Transaction tables are an optional feature: nodes optimized purely for consensus can run without them. Returning `rpcNOT_ENABLED` here rather than an empty result or a runtime error is the correct signal — it tells callers definitively that the capability is absent, not that no history exists.

## Access Control and Resource Accounting

Two distinct safeguards govern who can call this endpoint and how heavily:

**Resource classification.** The handler sets `context.loadType = Resource::feeMediumBurdenRPC` before doing any real work. This label feeds into the connection-level resource manager, which uses it for throttling decisions. Classifying `tx_history` as medium-burden reflects the fact that it issues a database scan rather than a point lookup, but it isn't as expensive as operations that touch consensus state or produce large cryptographic proofs.

**Deep-pagination cap.** Any `start` index greater than 10,000 is rejected with `rpcNO_PERMISSION` unless `isUnlimited(context.role)` returns true. The `Role` enum distinguishes `ADMIN` and `IDENTIFIED` (both unlimited) from `USER` and `GUEST` (limited). This cap is a pragmatic defense: scanning tens or hundreds of thousands of historical transactions is expensive for the backing SQLite database, and there is no legitimate reason an unprivileged client needs to page that deep. Privileged callers — internal tooling, administrative scripts — can bypass the cap cleanly without requiring a separate endpoint or a special parameter flag.

## Data Flow

Input validation proceeds in a strict sequence: feature-flag check → presence check for `start` → unsigned integer conversion → range/permission check. The `asUInt()` call on the JSON field is not strictly safe against malformed input (a non-integer value produces an implicit `0` rather than a type error), but this is consistent with how other XRPL RPC handlers treat loosely-typed JSON integers, and a `start` of `0` is a valid and harmless request.

After validation, `getRelationalDatabase().getTxHistory(startIndex)` performs the actual retrieval, returning a `std::vector<std::shared_ptr<Transaction>>`. The handler then iterates this vector, calling `t->getJson(JsonOptions::none)` on each entry and appending the result to the `txs` JSON array. The response also echoes the requested `index` value, giving clients a stable way to track their pagination position across calls.

The one non-trivial post-processing step is the call to `RPC::insertDeliverMax(tx_json, txnType, apiVersion)` for each transaction. This injects the `DeliverMax` field into Payment transaction JSON and, for API versions greater than 1, removes the legacy `Amount` field entirely. The same pattern appears in `Tx.cpp` and `TransactionEntry.cpp` — it is a cross-cutting compatibility shim that must be applied uniformly wherever raw transaction JSON surfaces to callers, because clients using newer API versions must not see the old field name.

## Design Observations

The handler is intentionally minimal. It performs no filtering by account or transaction type, no metadata enrichment beyond `DeliverMax`, and no ledger validation. This is appropriate: `tx_history` is a raw log browser, not a query engine. More sophisticated filtering belongs in higher-level tooling built on top of this primitive. The consequence is that the function's correctness guarantees are equally simple — as long as the config check, parameter validation, and role check pass, the result is exactly what `getTxHistory` returns, formatted as JSON.