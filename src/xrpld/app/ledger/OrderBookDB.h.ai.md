# `OrderBookDB.h` — Order Book Index Interface

## Role in the System

`OrderBookDB` is the pure abstract interface for the component that maintains an in-memory index of every active order book in the XRP Ledger. An "order book" in XRPL terms is a directional market pair: a `Book` specifies an `in` asset (what the taker pays) and an `out` asset (what the taker receives), plus an optional `domain` for permissioned DEX environments. The `OrderBookDB` exists to answer two different client needs efficiently: pathfinding queries ("what currencies can I reach from this asset?") and WebSocket subscriptions ("notify me when a trade happens in this market").

The file at `src/xrpld/app/ledger/OrderBookDB.h` is a stub pointing into the public include tree; the authoritative interface lives at `include/xrpl/ledger/OrderBookDB.h`. The concrete implementation is split across `OrderBookDBImpl.h` and `OrderBookDBImpl.cpp` in the same `src/xrpld/app/ledger/` directory.

## Interface Design

The `OrderBookDB` abstract class exposes six virtual methods that split cleanly into two concerns.

**Index maintenance:** `setup()` and `addOrderBook()` keep the in-memory book map current. `setup()` is the periodic full rebuild — it is called when a new ledger is accepted and triggers a full scan of all ledger objects to reconstruct the book index from scratch. `addOrderBook()` is the incremental path, used when a new offer entry becomes known outside the full scan cycle.

**Query and subscription:** `getBooksByTakerPays()` returns all books where the taker pays a given asset — the core primitive for pathfinding. `getBookSize()` returns the count without materializing the full vector. `isBookToXRP()` is a fast predicate checking whether any book converts a given asset into XRP, which pathfinding uses as a special hop. `getBookListeners()` and `makeBookListeners()` expose the `BookListeners` objects that dispatch transaction notifications to subscribed WebSocket clients.

The `domain` parameter appearing on all three query methods reflects XRPL's support for permissioned DEX pools. Books can be scoped to a `Domain` (a `uint256` identifier), and the index maintains entirely separate structures for domain-scoped books versus global books, preventing domain lookup from bleeding into general pathfinding.

## Implementation: Lazy Full Scans with Throttling

`OrderBookDBImpl::setup()` deliberately avoids rescanning the ledger on every close. It compares the incoming ledger's sequence against the last-updated sequence (`seq_`, an `std::atomic<uint32_t>`). If the new ledger is within 25,600 sequences ahead, or within 16 sequences behind, the update is skipped. This asymmetric tolerance handles both the normal forward progression of the ledger and scenarios like brief reorganizations. The atomic compare-exchange pattern (`seq_.exchange`) also ensures that if two threads race to trigger a full update, only one proceeds.

In networked mode, the full scan is dispatched as a `jtUPDATE_PF` job to the `JobQueue`, keeping the validation and consensus threads unblocked while the potentially expensive full ledger traversal runs in the background. In standalone mode it runs synchronously since there are no competing threads to starve.

## Update Strategy: Build-Then-Swap

`OrderBookDBImpl::update()` builds completely fresh copies of all four internal maps — `allBooks_`, `xrpBooks_`, `domainBooks_`, and `xrpDomainBooks_` — into local variables, walking every `ltDIR_NODE` that carries an `sfExchangeRate` (the marker for an order book directory) and every `ltAMM` entry. Only after the full scan completes does it acquire `mLock` and swap the locals into the member fields atomically. This minimises lock-hold time: readers are only blocked during the pointer swap, not during the multi-second traversal. If a `SHAMapMissingNode` exception is thrown mid-scan (a common occurrence during ledger acquisition), `seq_` is reset to zero to force a re-attempt on the next call.

## Concurrency Model

All public methods that touch the in-memory maps take `mLock`, a `std::recursive_mutex`. The recursive variant is necessary because `makeBookListeners()` calls `getBookListeners()` while already holding the lock — both methods are `public` and `getBookListeners()` also acquires the same mutex. The `seq_` atomic is used independently of `mLock` for the setup/update throttling logic, since it only guards a single integer and the check-then-act pattern uses `exchange` rather than load/store.

## Transaction Notification Path

`processTxn()` is called for every transaction applied to an accepted ledger. It walks the transaction metadata looking for `ltOFFER` nodes (created, modified, or deleted offers) and resolves the corresponding `BookListeners` for each affected book. The key design detail is `havePublished`, a `hash_set<uint64_t>` of subscriber IDs built locally per transaction. If a single transaction touches multiple offers in the same book, or if a client is subscribed to multiple books affected by one transaction, the set prevents duplicate delivery. This deduplication lives at the transaction level rather than inside `BookListeners::publish()`, keeping the listener logic simple.

## Data Model

`Book` holds two `Asset` fields (`in`, `out`) plus `std::optional<uint256> domain`. `Asset` is a variant over `Issue` (currency + issuer account) and `MPTIssue` (a multi-purpose token identifier), which means the same `OrderBookDB` infrastructure covers both classical IOU markets and the newer MPT-based markets without requiring separate indexes. The internal maps use `hardened_hash_map` — a hash-flooding-resistant variant — because the keys are derived from ledger data and therefore attacker-influenced; a standard `unordered_map` would be vulnerable to deliberate hash collisions that degrade lookups to O(n).