# `OrderBookDBImpl.h` — Order Book Index and Subscription Dispatcher

## Role in the System

`OrderBookDBImpl` is the concrete implementation of the `OrderBookDB` interface, which maintains a live in-memory index of all order books present in the XRP Ledger. Its two primary responsibilities are distinct but coupled: first, it provides fast lookup structures that pathfinding and other subsystems query to discover which currency pairs have active markets; second, it acts as a subscription fanout engine, routing accepted-ledger transactions to WebSocket clients who have subscribed to specific books.

The header declares the `OrderBookDBConfig` plain struct that carries the two configuration knobs — `pathSearchMax` and `standalone` — as well as the `make_OrderBookDB` factory function that follows the XRPL convention of hiding the concrete type behind a factory that returns `unique_ptr<OrderBookDB>`.

## Data Structures and Their Design Choices

The private state reveals careful attention to both performance and security:

```
hardened_hash_map<Asset, hardened_hash_set<Asset>>           allBooks_;
hardened_hash_map<pair<Asset,Domain>, hardened_hash_set<Asset>> domainBooks_;
hash_set<Asset>                                               xrpBooks_;
hash_set<pair<Asset,Domain>>                                  xrpDomainBooks_;
```

The main book maps use `hardened_hash_map` — a hash map whose seed is randomized at construction time using `std::random_device` — rather than a plain `hash_map`. This is a DoS-hardening measure: `Asset` values are derived directly from untrusted ledger data, and a malicious actor could submit transactions containing crafted currency/issuer combinations that all collide in a predictable hash table, degrading lookup from O(1) to O(n). By randomizing the hash seed per process instance, the collision structure becomes unpredictable to the attacker.

The `xrpBooks_` and `xrpDomainBooks_` sets are `hash_set` (standard, non-hardened) and exist purely as fast existence tests. The asymmetry reflects that these are consulted millions of times during pathfinding but have a bounded size (one entry per currency that has an XRP book), making them a legitimate performance optimization.

The `mListeners` map uses a plain `hash_map<Book, BookListeners::pointer>` because its keys are locally trusted (the node registers them) and not subject to adversarial injection.

`seq_` is `std::atomic<uint32_t>` and doubles as a version stamp and a compare-and-exchange gate that prevents redundant full scans.

## The `setup()` / `update()` Split

`setup()` is called on every accepted ledger but is intentionally cheap for common cases. It applies two skip conditions before doing any work:

- If the ledger is between 1 and 25599 sequences ahead of the last full update, skip — the in-memory index is still accurate enough.
- If the ledger is within 15 sequences behind the stored sequence (a minor reorg), skip.

If neither condition fires, `setup()` does a `seq_.exchange(ledger->seq())` and checks whether another thread already claimed this ledger; only one caller wins the race. The winner either calls `update()` directly (standalone mode) or enqueues it as a `jtUPDATE_PF` job on the JobQueue (networked mode), keeping the ledger-application hot path non-blocking.

`update()` performs the full ledger walk. It iterates every state ledger entry (`ledger->sles`), identifying `ltDIR_NODE` entries that carry an `sfExchangeRate` field with `sfRootIndex == key()` — those are the canonical roots of offer directories, i.e., a single price level in an order book. It reconstructs each `Book` from `sfTakerPaysCurrency`/`sfTakerPaysIssuer` or the newer `sfTakerPaysMPT` field (for Multi-Purpose Tokens), handles optional `sfDomainID` for domain-restricted books, and separately handles `ltAMM` entries which expose two implicit reverse books for their asset pair. All of this populates local copies of the four maps; once the walk completes, the maps are swapped under `mLock` in a single critical section, making the update atomic from the perspective of readers. If the process is stopping or a `SHAMapMissingNode` exception is thrown mid-scan, `seq_` is reset to 0 so the next `setup()` call will trigger a fresh full rebuild rather than skipping.

## Transaction Subscription Dispatch via `processTxn()`

When a ledger is accepted and applied, `processTxn()` is called for each transaction. It iterates the transaction metadata nodes, looking for affected `ltOFFER` entries. For each modified, created, or deleted offer node it extracts the `TakerPays`/`TakerGets` amounts to reconstruct the book identity, then looks up the corresponding `BookListeners` and calls `publish()`.

The `hash_set<uint64_t> havePublished` local variable is a deduplication guard: a single transaction may touch dozens of offer nodes spread across many price levels of the same book, or touch multiple books to which a single client has subscribed. Without this guard, the same client could receive the same transaction notification many times. `BookListeners::publish()` records each subscriber's ID in `havePublished` before sending, and skips it on subsequent calls within the same transaction.

## Concurrency Model

All mutable state is protected by `mLock`, declared as `std::recursive_mutex`. The recursive variant is necessary because `makeBookListeners()` calls `getBookListeners()` while already holding the lock, and `processTxn()` also holds the lock while calling `getBookListeners()`. This is a deliberate convenience rather than an oversight — the critical sections are short and `recursive_mutex` avoids introducing a separate unlocked internal helper.

The only lockless coordination is between `setup()` calls via `seq_`, which uses `std::atomic` compare-and-exchange to ensure that only one `update()` job is enqueued per ledger sequence, even under concurrent invocations from different threads.

## Relationship to Pathfinding Configuration

`pathSearchMax_` is the primary gate for whether pathfinding is active at all. Both `setup()` and `update()` return immediately when it is zero, meaning the entire order book index remains empty — a deliberate operational mode that allows a node operator to disable pathfinding to reduce memory and CPU cost. The `standalone_` flag affects only the threading model for `update()`, not its correctness.