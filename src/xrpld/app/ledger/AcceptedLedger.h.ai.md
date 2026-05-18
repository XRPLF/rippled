# `AcceptedLedger.h` — Post-Consensus Ledger Transaction View

## Role in the System

`AcceptedLedger` is a lightweight materialization layer sitting between a raw validated ledger and the subsystems that need to iterate its transactions — specifically the network subscription publisher (`NetworkOPs`) and the relational database backend (`Node.cpp`). Its single job is to take a `ReadView const` and produce a sorted, eagerly-constructed sequence of `AcceptedLedgerTx` objects that callers can range-iterate without touching the underlying SHAMap again.

The header carries an important terminology note. In XRPL parlance the word "accepted" is used loosely: a ledger that is "closed" has passed its close time and admits no new transactions; one that is "accepted" has gone through the consensus process and is believed to be correct, but may not yet have crossed the full validation threshold from a quorum of validators; once it does, it becomes "validated." The comment in the file acknowledges this ambiguity and flags it for cleanup. In practice the class is constructed and cached only for ledgers that have already accumulated sufficient validations, so the distinction is mostly historical.

## Construction and Transaction Ordering

The constructor in `AcceptedLedger.cpp` iterates `ledger->txs` — the ordered transaction/metadata pairs stored in the `ReadView` — and wraps each pair in a `std::unique_ptr<AcceptedLedgerTx>`. After collecting all transactions it sorts them by `getTxnSeq()`, which maps to the transaction's position index recorded in its `TxMeta`. This sort step is the key reason `AcceptedLedger` exists as a separate object: the underlying `ReadView` stores transactions in SHAMap order (keyed by transaction hash), not in execution order. Downstream consumers — especially those writing to SQL databases or delivering ordered event streams to clients — need a stable, sequence-ordered view. By sorting once at construction time, all subsequent consumers get O(1) random access with no repeated sorting.

The constructor pre-reserves 256 slots before populating the vector, which amortizes reallocation cost for a typical block of transactions. The reserve call appears twice in the implementation, which is a harmless but redundant duplication.

## Ownership and Caching

`AcceptedLedger` holds a `std::shared_ptr<ReadView const>` to keep the underlying ledger alive for as long as the accepted view exists. The `transactions_` vector holds `std::unique_ptr<AcceptedLedgerTx>`, giving exclusive ownership of the enriched transaction wrappers. This two-tier ownership model means the class itself is always managed via `std::shared_ptr<AcceptedLedger>` by callers — and that is exactly the type stored in the application-wide `TaggedCache<uint256, AcceptedLedger>`.

Both primary call sites in `NetworkOPs.cpp` and `Node.cpp` follow the same pattern: attempt a cache lookup by ledger hash, and only construct a new `AcceptedLedger` on a miss, then insert it into the cache with `canonicalize_replace_client`. This ensures that when multiple consumers process the same validated ledger (e.g., both publishing to WebSocket subscribers and writing to the database), the sort and allocation work happens at most once per ledger. Hit rate and cache size are exposed through the `get_counts` admin RPC as `AL_hit_rate` and `AL_size`, giving operators visibility into cache efficiency.

## `AcceptedLedgerTx` — The Enriched Transaction Wrapper

Each element in the `transactions_` vector is an `AcceptedLedgerTx` (defined in `include/xrpl/ledger/AcceptedLedgerTx.h`), which bundles together the raw `STTx`, the associated `TxMeta`, a `flat_set<AccountID>` of affected accounts, a serialized metadata blob, and a pre-built `Json::Value`. This per-transaction enrichment is what `InfoSub` needs to dispatch targeted account notifications to WebSocket subscribers — rather than re-parsing metadata on every delivery, the affected-accounts set is computed once at `AcceptedLedgerTx` construction time.

## Diagnostic Instrumentation via `CountedObject`

`AcceptedLedger` inherits from `CountedObject<AcceptedLedger>`, which uses a static `CountedObjects::Counter` with a lock-free linked-list registration to track the number of live instances. The counter increments on construction and decrements on destruction. This adds zero overhead to normal operation — the counter is an `std::atomic<int>` — while enabling the `get_counts` handler to report how many `AcceptedLedger` objects are resident in memory at any moment, which complements the cache size metric.

## Interface Design

The public interface is intentionally minimal: `getLedger()` returns the underlying `ReadView`, `size()` returns the transaction count, and `begin()`/`end()` return iterators into the sorted `transactions_` vector. There is no random-access by index or by transaction ID; consumers are expected to iterate the full sequence. This matches the two real use cases — sequential SQL writes and sequential event fan-out — and avoids the complexity of building secondary indices that are never needed.