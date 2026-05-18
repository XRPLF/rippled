# `LedgerHistory.cpp` — Ledger Cache and Consensus Mismatch Detector

`LedgerHistory` sits at the intersection of two concerns that happen to share data: serving historical ledgers quickly via an in-memory cache, and detecting when this node's locally-built ledger disagrees with the one the network has validated. The class is consumed primarily by `LedgerMaster` and the consensus subsystem; operators see its effects through the `ledger.history.mismatch` metric and the diagnostic log output it produces on divergence events.

## Cache Architecture

The class maintains two `TaggedCache` instances and one unbounded `std::map`.

`m_ledgers_by_hash` is the primary cache, mapping `LedgerHash → Ledger const`. Its capacity and TTL are drawn from application configuration (`SizedItem::ledgerSize` and `SizedItem::ledgerAge`), so they scale with the node's resource profile. Lookups first check the cache; on a miss, `loadByHash` or `loadByIndex` pull the ledger from the database and populate the cache for future access.

`mLedgersByIndex` is a plain `std::map<LedgerIndex, LedgerHash>` that records only validated ledgers — it is the index-to-hash mapping needed to answer "what hash does sequence N map to?" without touching the database. This map is not protected by its own mutex; instead it shares `m_ledgers_by_hash.peekMutex()`. Co-locating them under one lock prevents the sequence-to-hash mapping from diverging from the hash-to-ledger cache in concurrent scenarios. The acknowledged technical debt (`// FIXME: Need to clean up ledgers by index at some point`) is that `mLedgersByIndex` grows without bound — `clearLedgerCachePrior` prunes the hash cache but does not touch the index map.

`m_consensus_validated` is a smaller, second `TaggedCache` (64 entries, 5-minute TTL) keyed on `LedgerIndex` and holding `cv_entry` structs. Each `cv_entry` stores the optional hash of the locally-built ledger, the optional hash of the validated ledger, both consensus transaction set hashes, and the consensus JSON snapshot for that round. This cache is the core data store for the mismatch detection machinery.

## The Immutability Invariant

Every ledger entering the history is required to be immutable. `insert()` enforces this with a hard `LogicError` (not a recoverable exception — this is a programming contract violation). `getLedgerBySeq()` and `getLedgerByHash()` assert immutability on ledgers loaded from the database before caching them. The reason is structural: `TaggedCache` permits shared ownership across multiple callers; if any caller could mutate a cached ledger, all concurrent readers would observe inconsistent state. By requiring immutability at the boundary, the class can safely distribute `shared_ptr<Ledger const>` without further synchronization.

## `insert()` — Cache Population

`insert()` is the entry point when a new ledger arrives. It calls `canonicalize_replace_cache` on `m_ledgers_by_hash`, which means: if an object for this hash already exists in the cache, replace the cached copy with the incoming one (the caller's version "wins"). This is the right policy here because the insert caller typically holds a fresher, just-built ledger. If `validated` is true, the sequence-to-hash mapping in `mLedgersByIndex` is also recorded. The method returns `true` if the hash was already present, letting callers detect duplicate insertions.

`getLedgerBySeq()` and `getLedgerByHash()` use `canonicalize_replace_client` on cache population after a database load: if the cache already holds a canonical instance, the caller's newly-loaded pointer is replaced with the cached one. This ensures that all code paths end up sharing a single object per unique ledger, avoiding redundant memory consumption.

## Mismatch Detection: `builtLedger()` and `validatedLedger()`

These two methods implement a rendezvous pattern. Each records its event in `m_consensus_validated` and checks whether the other event for the same sequence has already arrived.

`builtLedger()` is called when local transaction processing for a round completes. It creates (or retrieves) a `cv_entry` for the sequence number, and if `validated` is already populated but `built` is not, it compares the two hashes. If they differ, `handleMismatch()` is triggered. The consensus hash and JSON snapshot of the round are stored in the entry regardless, so `validatedLedger()` can access them later.

`validatedLedger()` mirrors this logic for the network-validation path. Because either event can arrive first — the node might validate before finishing local construction, or finish building before receiving the validation — both methods defensively check only the "other side is present and I am absent" condition before comparing.

## `handleMismatch()` — Fault Triage

When the built and validated hashes differ, `handleMismatch()` performs a structured triage to classify the failure mode:

1. **Parent hash mismatch**: the two ledgers have different parents, indicating this node built on the wrong prior ledger (a sync or fork issue, not a determinism problem).
2. **Close time mismatch**: Byzantine agreement failure — validators chose a different close time, meaning the disagreement is at the consensus protocol level, not the execution engine.
3. **Consensus transaction set mismatch** (if both hashes are available): the transaction sets that were applied actually differ. If the transaction sets match, the error is more serious: same inputs produced different outputs.

After classification, the method calls `leaves()` to extract the transaction maps of both ledgers as sorted vectors of `SHAMapItem` pointers, then walks the two sorted lists in parallel — a classic set-difference merge. Transactions present in one but not the other are logged via `log_one()`; transactions present in both but with different metadata (same tx, different outcome) are passed to `log_metadata_difference()`, which compares result code (`TER`), index, and state-change nodes independently to isolate exactly which field diverged. The mismatch counter `mismatch_counter_` is incremented every time the method is entered, making the event visible to external monitoring infrastructure through `beast::insight`.

## `fixIndex()` and `clearLedgerCachePrior()`

`fixIndex()` corrects a stale or wrong sequence-to-hash mapping, returning `false` to signal that a repair was actually performed. This is used when a more authoritative hash for a given ledger index arrives after the mapping was initially set — for example, when processing validation messages from peers.

`clearLedgerCachePrior()` is a bulk eviction operation. It iterates all keys in `m_ledgers_by_hash`, loads each ledger by hash, and removes any whose sequence number falls below the given threshold. This is used when the node needs to shed old cache entries ahead of a targeted cleanup, complementing the TTL-based automatic sweeping done by `sweep()`.