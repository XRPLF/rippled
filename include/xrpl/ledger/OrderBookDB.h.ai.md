# `include/xrpl/ledger/OrderBookDB.h`

## Purpose and Context

`OrderBookDB` is a pure abstract interface that defines how the XRPL server tracks and queries all active order books across the ledger. An order book in XRPL terms is a directed trading pair — a set of open `ltOFFER` entries that all share the same "taker pays" and "taker gets" assets. Because pathfinding and client subscriptions both need fast lookups of which markets exist, this index is maintained separately from the ledger state itself.

The interface lives in the public `include/xrpl/ledger/` layer, keeping it decoupled from the concrete implementation (`OrderBookDBImpl`, in `src/xrpld/app/ledger/`). Callers interact only with `OrderBookDB*`, while the concrete type is instantiated through `make_OrderBookDB(ServiceRegistry&, OrderBookDBConfig const&)` and injected via the service registry. This separation enables testing and keeps the heavy implementation details out of consumer headers.

## The `Book` and `Asset` Types

`Book` represents a directed trading pair: `in` (what the taker pays) and `out` (what the taker gets), plus an optional `domain`. The `domain` field is a `uint256` that scopes the book to a permissioned domain — a newer XRPL feature where certain books are only accessible to participants in a specific domain. Global books leave `domain` as `std::nullopt`.

`Asset` is a `std::variant<Issue, MPTIssue>`, abstracting over the three asset kinds XRPL supports: XRP and IOU (both wrapped in `Issue`) and the newer Multi-Purpose Token standard (`MPTIssue`). The `OrderBookDB` interface uses `Asset` throughout rather than the older `Issue` type, so all query methods work uniformly across traditional and MPT token pairs.

## Interface Methods

**`setup()`** is the entry point called on each accepted ledger. Rather than always performing a full ledger scan, the implementation throttles these scans intelligently: it skips if the new ledger is within 25,600 sequences ahead of the last scanned ledger (incremental transactions keep the index current via `processTxn`) or within 16 sequences behind it (a small reorg). Outside these windows a full scan is enqueued. In non-standalone mode this scan runs as a background job on the job queue; in standalone mode it runs synchronously. The scan walks every `ltDIR_NODE` with an `sfExchangeRate` field (which marks order book directory roots) and every `ltAMM` object, rebuilding the entire in-memory book maps in local variables before swapping them under a lock, so readers are never blocked for long.

**`getBooksByTakerPays()`** is the primary pathfinding query. Given an asset and optional domain, it returns every `Book` that has that asset as its "in" side — i.e., every market where you can spend that asset. The pathfinding engine calls this at each hop to enumerate possible next steps toward the destination currency.

**`getBookSize()`** returns the number of distinct "out" assets available for a given "in" asset. This count is used as a heuristic to limit pathfinding breadth.

**`isBookToXRP()`** answers a fast yes/no question: does any order book exist where the given asset can be sold for XRP? The implementation keeps a separate `xrpBooks_` set (and `xrpDomainBooks_` for permissioned variants) so this check is O(1) without scanning `allBooks_`. Pathfinding uses this to identify assets that can be liquidated directly to XRP without an intermediate hop.

**`addOrderBook()`** allows callers to register a single book without triggering a full scan. This handles the case where a new book is discovered incrementally (e.g., from `processTxn`) before the next scheduled full update.

## Transaction Processing and Subscriptions

**`processTxn()`** is called for every transaction in a closed ledger. It walks the transaction's metadata nodes looking for `ltOFFER` entries that were created, modified, or deleted, then extracts the `TakerGets` and `TakerPays` fields from the appropriate snapshot (`sfNewFields`, `sfPreviousFields`, or `sfFinalFields`). For each touched offer, it looks up a `BookListeners` object keyed by the reversed book (`TakerGets` → `TakerPays`), and if subscribers exist, calls `publish()`.

A critical correctness detail: a single transaction may touch multiple offers in the same book, or a client may have subscribed to multiple books that one transaction affects. Without deduplication, the same transaction would be delivered to a subscriber multiple times. `processTxn()` solves this by maintaining a `hash_set<uint64_t> havePublished` local to each call, tracking the unique subscriber IDs that have already received the message during this invocation. `BookListeners::publish()` checks and updates this set, so each subscriber receives at most one notification per transaction regardless of how many of its books were touched.

**`getBookListeners()`** / **`makeBookListeners()`** manage the subscription map. `getBookListeners()` returns `nullptr` if no subscribers exist for a book, while `makeBookListeners()` creates a new `BookListeners` entry on demand. The separation avoids creating empty listener objects for every book that passes through the system.

## Concurrency Design

All internal maps in `OrderBookDBImpl` are guarded by a `std::recursive_mutex`. The recursive nature is required because `makeBookListeners()` calls `getBookListeners()` under the same lock. The expensive `update()` scan builds new maps entirely outside the lock and then does a fast `swap()` inside a brief critical section, so reader calls like `getBooksByTakerPays()` and `processTxn()` are only briefly blocked during the final swap rather than for the duration of a full ledger traversal.