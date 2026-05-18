# `TransactionMaster.cpp` — Application-Level Transaction Cache and Retrieval

`TransactionMaster` is the single point of authority for in-memory transaction state across the XRPL node. It wraps a `TaggedCache<uint256, Transaction>` and presents a unified lookup API used by everything from RPC handlers and the P2P overlay to the consensus engine and the database persistence layer. Its core mission is to avoid redundant database reads by keeping recently-seen `Transaction` objects alive in a shared-pointer cache, while also ensuring that all subsystems that hold a reference to a given transaction share the same object.

## Cache Configuration

The constructor allocates a `TaggedCache` named `"TransactionCache"` with a capacity of **65,536 entries** and a **30-minute TTL**. Those parameters are hard-coded at construction time via `Application&`, which also supplies the `stopwatch()` used for time-based expiry and the `beast::Journal` instance for cache diagnostics. The class is non-copyable by design — it is a singleton-like application service instantiated once in `ApplicationImp` and returned by `Application::getMasterTransaction()`.

## Three `fetch` Overloads

The three `fetch` overloads each serve a different caller context, but they share a common philosophy: check the in-memory cache first; fall back to persistent storage only when necessary.

**Hash-only fetch** (`fetch(uint256, error_code_i&)`) and its **range-bounded variant** (`fetch(uint256, ClosedInterval<uint32_t>, error_code_i&)`) are the entry points used by RPC handlers and the `tx` command. Both begin with `fetch_from_cache()`. The subtle decision that follows is worth noting: if the cache hit is an *unvalidated* transaction (`isValidated()` returns `false` when `mLedgerIndex == 0`), the function returns it immediately without hitting the database. An unvalidated transaction is still pending — it has no ledger metadata — so there is nothing to load from storage anyway. If the cached transaction *is* validated (already committed to a closed ledger), the code bypasses the cache hit and delegates to `Transaction::load()` to retrieve the `TxMeta` alongside it. The cache alone never stores metadata; metadata only exists in the database after ledger close.

After a successful `Transaction::load()`, the freshly-loaded object is inserted or deduplicated in the cache via `canonicalize_replace_client()` before returning to the caller.

The **`ClosedInterval` overload** passes the ledger range through to `Transaction::load()`, which restricts the database search to those ledger sequences. If the searched range was not fully present in the database at query time, `load()` returns a `TxSearched` enum rather than a transaction pair; `fetch` propagates this variant as-is so callers can distinguish "transaction doesn't exist" from "we can't be sure — our history is incomplete."

**SHAMap-item fetch** (`fetch(SHAMapItem, SHAMapNodeType, uCommitLedger)`) is used during ledger application when iterating over a `SHAMap`'s transaction leaves. The function handles two distinct wire encodings. For `tnTRANSACTION_NM` (no metadata) nodes, the item's raw slice is fed directly into a `SerialIter` and used to construct an `STTx`. For `tnTRANSACTION_MD` nodes (transaction with embedded metadata), the serialized format prefixes the transaction bytes with a variable-length header, so `getVL()` is called to extract the inner transaction blob before the `STTx` is constructed. Neither branch updates the `mCache` — this overload returns a `shared_ptr<STTx const>` rather than a `Transaction` wrapper, reflecting that its callers only need the raw protocol object during ledger processing. When the item *is* found in cache, the cached `Transaction`'s status can be updated to `COMMITTED` if `uCommitLedger` is non-zero, propagating commit information back to any other holder of that shared pointer.

## Canonicalization

`canonicalize(std::shared_ptr<Transaction>*)` and the internal call to `canonicalize_replace_client()` enforce a deduplication invariant: for any transaction hash, there is exactly one live `Transaction` object. `TaggedCache::canonicalize_replace_client()` looks up the key under its internal lock; if the key already exists (cache hit), the caller's pointer is **redirected to the existing cached instance** (the "client" pointer is replaced). If the key is absent, the new object is inserted and the caller's pointer is left pointing at it. The net effect is that all in-flight holders — `NetworkOPs`, `PeerImp`, any active RPC response — end up pointing at the same `Transaction` object, so a `setStatus(COMMITTED, ...)` call from any one of them is immediately visible to all others through the shared reference without any additional synchronization.

`canonicalize()` guards against inserting a zero hash (`beast::zero` check), which would correspond to a `Transaction` that failed its own construction and has no valid ID.

## Status Propagation via `inLedger()`

`inLedger()` is called by the database write-back path in `Node.cpp` when a transaction is being persisted after ledger close. If the transaction is in cache, `setStatus(COMMITTED, ledger, tseq, netID)` is called on it; otherwise the function returns `false`, meaning the transaction was never tracked in memory and no in-memory state needs updating. The return value is used by callers as an "already knew about this" signal.

## Sweep and Cache Exposure

`sweep()` delegates directly to `TaggedCache::sweep()`, which evicts entries whose strong references have expired and whose weak references can no longer be promoted. It is called periodically by `ApplicationImp`'s sweep loop, which logs the cache size before and after. `getCache()` exposes a mutable reference to the underlying `TaggedCache` directly; `SHAMapStoreImp` uses this during online deletion to freshen the cache, preventing valid entries from being incorrectly evicted during a history rotation.

## Thread Safety

`TransactionMaster` itself performs no locking. Thread safety is provided entirely by `TaggedCache`'s internal `std::mutex`, which serializes all insertions, lookups, and canonicalize operations. The `Transaction::mApplying` flag and `SubmitResult` state are explicitly excluded from this protection model — per the comments in `Transaction.h`, those fields are accessed only under `NetworkOPsImp`'s own lock, and a rare race has been accepted as a deliberate tradeoff since the worst consequence is a redundant transaction attempt.