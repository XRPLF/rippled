# `LedgerHistory.h` — Historical Ledger Cache and Mismatch Detector

`LedgerHistory` is the in-memory ledger registry for `LedgerMaster`. It serves two distinct purposes that, while related, operate largely independently: fast retrieval of historical ledgers by hash or sequence number, and a per-sequence consensus comparison mechanism that can detect Byzantine failures or determinism bugs in transaction processing.

## Storage Architecture

The class maintains three separate data structures with different lifetimes and purposes.

`m_ledgers_by_hash` is a `TaggedCache<LedgerHash, Ledger const>` — the primary object store. Its capacity and TTL are drawn from `SizedItem::ledgerSize` and `SizedItem::ledgerAge` in the application config, making it scale with node memory settings. `TaggedCache` tracks objects both by strong (cached) and weak (tracked) pointers, so a ledger remains findable as long as any code holds a `shared_ptr` to it, even after it ages out of the bounded cache window.

`mLedgersByIndex` is a plain `std::map<LedgerIndex, LedgerHash>` serving as a sequence-to-hash index for validated ledgers. Unlike the two caches, it is unbounded and never automatically pruned — a known issue called out with a `FIXME` comment in the `.cpp` file. This map is protected by reusing `m_ledgers_by_hash.peekMutex()` rather than maintaining its own mutex, tying its locking lifetime to the main cache.

`m_consensus_validated` is a second `TaggedCache<LedgerIndex, cv_entry>` capped at 64 entries with a 5-minute TTL. Each `cv_entry` stores optional hashes for the locally built ledger, the network-validated ledger, and the corresponding consensus transaction set hashes, plus the consensus metadata JSON. This cache exists solely for the mismatch-detection path.

## Retrieval and Disk Fallback

`getLedgerByHash()` first tries `m_ledgers_by_hash.fetch()`, then falls back to `loadByHash()` from `LedgerPersistence`, inserting the result back into cache on success.

`getLedgerBySeq()` has a more careful locking sequence: it holds `m_ledgers_by_hash.peekMutex()` to look up the hash from `mLedgersByIndex`, then explicitly releases the lock before calling `getLedgerByHash()`. This avoids a re-entrant lock acquisition because `getLedgerByHash()` also acquires the same mutex internally. When neither cache has the answer, it calls `loadByIndex()` and registers the result in both the hash cache and the index map.

Both retrieval paths enforce immutability with `XRPL_ASSERT(ret->isImmutable(), ...)`. The `insert()` path goes further — it raises a `LogicError` if a mutable ledger is presented. Only immutable ledgers are stored because `TaggedCache` explicitly prohibits modification of cached objects without external synchronization.

## Consensus Mismatch Detection

The `builtLedger()` and `validatedLedger()` methods record the outcomes of two separate events — the local node constructing a ledger from the consensus transaction set, and the network declaring a ledger fully validated. These events can arrive in either order, which the `cv_entry` struct accommodates with separate `built` and `validated` fields.

When the second event arrives for a given sequence, `builtLedger()` or `validatedLedger()` compares the stored hash from the first event against the incoming hash. A match logs a `MATCH` debug entry and is the expected case. A divergence increments `mismatch_counter_` (a `beast::insight::Counter` visible to monitoring systems) and triggers `handleMismatch()`.

`handleMismatch()` performs structured forensics to characterize the root cause:

1. **Different parent hashes** — the built ledger started from a different prior ledger; this is a sync/acquisition issue.
2. **Different close times** — the two ledgers agreed on transactions but disagree on when the ledger closed; this indicates a Byzantine actor misreporting timestamps.
3. **Different consensus transaction sets** — the hash of the agreed-upon transaction set differs, meaning the node processed different transactions than the network majority.
4. **Same transaction set, different outcome** — both ledgers contain identical transactions but produce different state. This is the most serious case: a determinism bug. `handleMismatch()` performs a sorted symmetric merge of the two ledgers' `txMap` leaves to identify transactions present in one but not the other, and for transactions present in both but with differing data, `log_metadata_difference()` drills into `TxMeta` to compare result codes, ledger entry indices, and SHAMap node lists.

The `leaves()` helper function extracts and sorts `SHAMapItem` pointers from a transaction map so the two-pointer merge loop can run in O(n) time over both sets simultaneously.

## Integration with LedgerMaster

`LedgerHistory` is a non-public data member of `LedgerMaster` and is not exposed as an interface on its own. All calls in `LedgerMaster.cpp` go through thin delegation methods (`addLedger()`, `getLedgerBySeq()`, etc.) that simply forward to the corresponding `LedgerHistory` methods. The `sweep()` method is called periodically from `LedgerMaster`'s sweep logic and delegates to both `TaggedCache` instances. `clearLedgerCachePrior()` removes entries from `m_ledgers_by_hash` for ledgers older than a given sequence — notably it does not clean `mLedgersByIndex`, which is the source of the lingering FIXME.

The `fixIndex()` method corrects a stale index mapping: if `mLedgersByIndex[ledgerIndex]` holds the wrong hash, it is updated in-place and `false` is returned to signal that a repair was needed. This is used during ledger acquisition when a previously cached mapping turns out to have been for an uncommitted fork.