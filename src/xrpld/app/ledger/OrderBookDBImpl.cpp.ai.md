# `OrderBookDBImpl.cpp` — In-Memory Order Book Index for Pathfinding and Subscriptions

`OrderBookDBImpl` is the concrete implementation of the `OrderBookDB` interface. It maintains an in-memory index of every order book and AMM pool present in the ledger, serving two distinct consumers: the pathfinding engine (which needs to know which books exist without scanning the ledger each time) and the WebSocket subscription system (which needs to fan out trade notifications to clients watching specific books).

## Data Model

The internal index is organized around the "taker pays" direction — that is, given an asset that a market participant is willing to pay, what are the possible assets they can receive? Four separate data structures cover the full address space:

- `allBooks_` — a `hardened_hash_map<Asset, hardened_hash_set<Asset>>` mapping each paying asset to the set of receivable assets for global (non-domain) books.
- `domainBooks_` — the same structure keyed by `pair<Asset, Domain>`, scoping books to a specific permissioned DEX domain.
- `xrpBooks_` / `xrpDomainBooks_` — fast-path sets recording which assets have at least one book leading to XRP, used by the pathfinder's XRP-termination heuristic without needing to iterate the full book set.

`Asset` is a variant type covering both traditional `Issue` (currency + issuer) and the newer `MPTIssue` (Multi-Purpose Token ID), so the index handles both asset kinds uniformly.

## Full Rebuild: `setup()` and `update()`

`setup()` is the entry point called each time a ledger is accepted. Its primary job is rate-limiting: a full ledger walk to rebuild the index is expensive, so the method suppresses redundant calls through two sequence number checks. If the new ledger is more than zero but fewer than 25,600 sequences ahead of the last full update, the call is silently skipped — this covers normal chain advance where the order book topology changes slowly. Conversely, if the new ledger is within 16 sequences *behind* the last update (possible during network catch-up or short-range reorg), it is also skipped to avoid regressing state. The threshold asymmetry (25,600 vs. 16) reflects that forward-advancing ledgers are far more common.

The compare-and-swap via `seq_.exchange()` prevents duplicate jobs from being enqueued if two threads call `setup()` concurrently with the same ledger. If the atomic exchange reveals that another caller already claimed this ledger sequence, the second caller returns immediately.

When an update is warranted, `update()` is dispatched to the `JobQueue` under job type `jtUPDATE_PF` in networked mode, or called inline in standalone mode. This async dispatch matters for performance: the ledger walk happens on a dedicated thread pool thread rather than blocking whatever called `setup()`.

`update()` performs a linear scan of all `sle` (serialized ledger entries) in the ledger snapshot. It looks for two entry types:

- **`ltDIR_NODE` with `sfExchangeRate`** at the root index: these are order book root directories. The entry carries `sfTakerPaysCurrency`/`sfTakerPaysIssuer` or `sfTakerPaysMPT` (and corresponding Gets fields), from which the full `Book` — including an optional `sfDomainID` — is reconstructed.
- **`ltAMM`**: AMM pools are synthetic two-sided books; both `(asset1 → asset2)` and `(asset2 → asset1)` directions are registered, since an AMM pool services swaps in either direction.

The update builds its new maps entirely into *local* variables, then acquires `mLock` only for the final `swap()` calls. This copy-and-swap pattern is intentional: the ledger walk may take milliseconds on a large ledger, and queries to `getBooksByTakerPays()` must not be blocked for the entire duration.

If `isStopping()` is detected mid-walk, the update resets `seq_` to `0` and returns, ensuring the next `setup()` call will trigger a fresh attempt rather than assuming the old data is still current. The same reset happens when a `SHAMapMissingNode` exception is caught, which occurs if the node store is incomplete — the incomplete result is discarded rather than partially replacing good data.

After a successful swap, `ledgerMaster.newOrderBookDB()` is called to wake any components that depend on order book availability.

## Incremental Updates: `addOrderBook()`

`addOrderBook()` offers a fast path for adding a single book without a full rebuild. This is used when a `jtOFFER_CREATE` transaction creates a new book that did not previously exist — it keeps the index current between full `setup()` cycles without the overhead of a ledger scan.

## WebSocket Subscription Pipeline

`processTxn()` handles the subscription fan-out side. When a transaction is accepted into a ledger, its metadata contains `sfModifiedNode`, `sfCreatedNode`, and `sfDeletedNode` entries. For each `ltOFFER` node, the method extracts the book identity from `sfTakerGets`/`sfTakerPays` in the appropriate field (`sfPreviousFields` for modifications, `sfNewFields` for creations, `sfFinalFields` for deletions) and calls `publish()` on the corresponding `BookListeners` instance.

The `havePublished` set (a `hash_set<uint64_t>`) is maintained per-transaction to avoid duplicate delivery. A single transaction can touch dozens of offer entries in the same book (as one big order crosses multiple resting offers), and a client might also subscribe to multiple books touched by one transaction. Without deduplication, a client would receive the same transaction JSON multiple times.

`BookListeners` itself stores weak references (`std::weak_ptr<InfoSub>`) to subscriber sessions. Expired sessions are pruned lazily during `publish()` when the weak pointer can no longer be promoted.

`makeBookListeners()` and `getBookListeners()` form an idempotent registry pattern: the former creates a `BookListeners` if one doesn't exist for a book, the latter is a read-only lookup. Both hold `mLock`; `makeBookListeners()` calls `getBookListeners()` internally, which requires `mLock` to be `std::recursive_mutex` rather than a plain mutex.

## Concurrency Summary

`seq_` is the only field accessed without `mLock`; it uses `std::atomic<uint32_t>` to coordinate the race between concurrent `setup()` callers and between `setup()` and the async `update()` job. Everything else — all four book maps, the listeners map — is guarded by `mLock`. The copy-and-swap in `update()` minimizes the critical section for the expensive rebuild path, while `processTxn()` and query methods hold the lock for the duration of their work since those operations are much shorter.