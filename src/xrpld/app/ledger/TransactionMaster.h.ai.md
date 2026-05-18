# `TransactionMaster` — In-Memory Transaction Cache and Lookup Coordinator

`TransactionMaster` is the single point of authority for `Transaction` objects that are live in process memory. Every subsystem that needs to look up, intern, or update the status of a transaction goes through this class rather than bypassing directly to the database or re-deserializing from a `SHAMapItem`. Its existence eliminates duplicate object allocation: two callers asking for the same hash receive the same `shared_ptr<Transaction>`, and status mutations (e.g., marking a transaction `COMMITTED`) performed by one caller are immediately visible to all others holding a reference.

## Core Storage

The backing store is a `TaggedCache<uint256, Transaction>` named `"TransactionCache"`, constructed in `Application` with a capacity ceiling of 65,536 entries and a 30-minute expiry. `TaggedCache` is a hybrid map-plus-LRU-cache: while a strong reference is held by the cache itself, the entry stays alive and can be retrieved by key. After eviction, the map retains a weak reference for as long as any outside caller holds a `shared_ptr`; those callers can therefore keep an entry alive past the TTL without holding a separate lock. The mutex discipline is entirely internal to `TaggedCache` (it uses a `std::recursive_mutex`), so callers of `TransactionMaster` do not need to take a separate lock.

## The `fetch` Family

Three overloads of `fetch()` serve different call sites:

**`fetch(hash, ec)`** is the RPC-layer workhorse, used by `Tx.cpp` when the `tx` command has no ledger range constraint. It first calls `fetch_from_cache(hash)`: if the result is present and already validated (i.e., `mLedgerIndex != 0`), the cache-only read would still miss its metadata, so the code deliberately skips that path and falls through to `Transaction::load()`. Conversely, if the cache hit is *un*validated, the transaction is returned immediately with a null `TxMeta` pointer — the caller asked about a transaction that hasn't landed in a closed ledger yet, so there is no metadata to return. After a database hit, `canonicalize_replace_client` inserts the freshly loaded object into the cache (or, if another thread raced and inserted first, replaces the caller's local pointer with the already-cached instance).

**`fetch(hash, range, ec)`** mirrors the above but threads a `ClosedInterval<uint32_t>` through to `Transaction::load()`. This range tells the database layer which ledger sequence window to search, enabling the return value's `TxSearched` variant to distinguish `TxSearched::All` (every ledger in the range was present), `TxSearched::Some` (some ledgers were missing), and `TxSearched::Unknown`. RPC clients use this to surface a correct response indicating whether a "not found" result is definitive or provisional.

**`fetch(SHAMapItem, type, uCommitLedger)`** serves consensus and ledger-application paths that already have the raw serialized bytes from the SHAMap. It first checks the cache by the item's key (hash). On a miss, it directly deserializes an `STTx` from the item's slice — handling both `tnTRANSACTION_NM` (transaction bytes only) and `tnTRANSACTION_MD` (transaction-plus-metadata, requiring an extra VL-length decode) node types. On a cache hit, if `uCommitLedger` is non-zero, the cached `Transaction` is updated to `COMMITTED` status before the `STTx` is returned. Notice that this overload returns a bare `shared_ptr<STTx const>`, not a `Transaction` wrapper — the SHAMap-based callers only need the immutable protocol object, not the lifecycle metadata.

## `canonicalize` — Object Deduplication

`canonicalize(std::shared_ptr<Transaction>*)` is a pointer-rewrite operation. The caller passes the address of its own `shared_ptr`; if the cache already holds an entry for that hash, `TaggedCache::canonicalize_replace_client` atomically replaces the caller's pointer with the cached instance. The net effect is that, after the call, the caller's variable points to whichever object the cache considers canonical. This matters when the same transaction arrives through multiple ingestion paths (peer messages, local submission, database replay) roughly simultaneously — rather than N separate heap objects, all callers converge on one. `NetworkOPs` calls this after locally submitting a transaction; `PeerImp` calls it when a transaction arrives over the network.

## `inLedger` — Status Promotion Without Insertion

`inLedger(hash, ledger, tseq, netID)` has a subtle contract: it returns `false` and does nothing if the transaction is *not already in the cache*. It intentionally does not fetch from the database or insert a new entry. Its sole purpose is to promote a transaction that is already cached — presumably from an earlier broadcast phase — to `COMMITTED` status now that the database layer has confirmed which ledger it landed in. This is called from `Node.cpp` during the SQLite-backed transaction loading path. The asymmetry (update-only, never insert) prevents stale database entries from populating the in-memory cache during bulk historical scans.

## Lifecycle

`TransactionMaster` is owned as a value member of `ApplicationImp`, constructed before the application starts processing and destroyed with it. `sweep()` is called periodically from the application's sweep loop (along with other caches) to evict expired entries and reclaim memory. `getCache()` exposes a direct reference to the `TaggedCache` for metrics collection and the sweep callback that `SHAMapStoreImp` registers to correlate the transaction cache with node-store rotation events.