# `src/libxrpl/ledger/Ledger.cpp`

## Role and Purpose

`Ledger.cpp` provides the concrete implementation of the `Ledger` class — the central data structure in the XRPL that represents a complete, cryptographically-committed snapshot of global state at a single point in time. Every operation in the ledger pipeline, from genesis bootstrap to consensus acceptance, flows through this file. It sits at the boundary between the high-level view abstractions (`ReadView`, `TxsRawView`) and the low-level content-addressed storage layer (`SHAMap`).

A `Ledger` object owns two `SHAMap` trees: `stateMap_` holds all persistent state entries (`SLE` objects — accounts, order books, escrows, etc.), and `txMap_` holds the transactions and their metadata that were applied to produce this ledger. The `LedgerHeader` bundles metadata including sequence number, close timestamps, the SHA-256 hash roots of both maps, total XRP in existence, and consensus timing parameters.

## Construction Variants

Five constructors exist, each serving a distinct lifecycle role:

**Genesis constructor** (`create_genesis_t`) builds ledger 1 from scratch. It hard-derives the "master" account by calling `generateKeyPair` on the string seed `"masterpassphrase"` and assigns it the entire `INITIAL_XRP` supply. This is a deterministic, network-wide constant. The constructor also conditionally writes fee parameters in either the classic integer format (`sfBaseFee`) or the newer `featureXRPFees` drops format (`sfBaseFeeDrops`), depending on which amendments are enabled at genesis. This forward-compatibility is key for testing environments that boot with modern amendments already active.

**Successor constructor** (`Ledger const& prevLedger, NetClock::time_point`) creates a new mutable ledger that follows a previous one. Crucially, it copies `prevLedger.stateMap_` with `true` (copy-on-write mode) so the new ledger starts with the full prior state without duplicating memory. The `txMap_` starts empty since no transactions have been applied yet. Close-time resolution is recalculated via `getNextLedgerTimeResolution`, which adjusts the resolution window up or down based on whether the previous ledger's close time achieved consensus.

**Load constructor** (`LedgerHeader, bool& loaded, bool acquire`) reconstructs a ledger from its serialized header and known SHAMap root hashes. It fetches the roots from the node store and marks `loaded = false` if either is missing, optionally triggering acquisition from the network via `family.missingNodeAcquireByHash()`.

**Header-only constructor** (`LedgerHeader, Rules, Family`) builds an immutable ledger for reference purposes when only the header is known — used in validation pipelines where the full state tree is not needed. It computes `header_.hash` immediately via `calculateLedgerHash`.

**Database constructor** constructs a blank mutable ledger for a given sequence and close time, used when hydrating ledgers from local storage.

## The Mutable/Immutable Transition

The most architecturally significant design decision is the strict mutable-to-immutable state machine. While mutable, a ledger is exclusive to one writer and needs no locking. `setImmutable(rehash=true)` finalizes the ledger by computing `txHash` and `accountHash` from the respective `SHAMap::getHash()` roots, then combining them via `calculateLedgerHash` into the canonical `header_.hash`. After this call, both SHAMaps are also marked immutable, preventing any further modifications. Immutable ledgers can then be safely shared across threads. The header comment documents this explicitly: mutable ledgers cannot be shared; immutable ones need no locks.

`setAccepted()` extends this by recording the consensus close time and setting `sLCF_NoConsensusTime` in `closeFlags` when the network did not agree on a precise close time. It then delegates to `setImmutable()` to finalize the hash.

## `setup()`: Deriving Runtime State from Ledger Content

`setup()` is called from both constructors and `setImmutable()`, and its job is to populate the in-memory `rules_` and `fees_` fields by reading the actual on-ledger SLEs. This design means network fee and amendment configuration is not stored separately — it is always derived from the ledger state itself, ensuring all nodes derive the same values from the same canonical data.

The fee loading handles a format migration: old ledgers store fees as plain integers (`sfBaseFee` as `uint64`, `sfReserveBase` as `uint32`), while post-`featureXRPFees` ledgers store them as `STAmount` in drops (`sfBaseFeeDrops`). The function validates that a ledger does not contain both formats simultaneously, and that new-format fees only appear after the `featureXRPFees` amendment activates. Either condition sets the return value to `false`, signalling a malformed ledger.

## Iterator Architecture

The inner classes `sles_iter_impl` and `txs_iter_impl` implement the type-erased `iter_base` interface defined in `ReadView::detail::ReadViewFwdRange`. Rather than exposing SHAMap iterators directly (which would leak the internal representation), `Ledger` wraps them in heap-allocated polymorphic objects and returns `std::unique_ptr<iter_base>`. The `sles_type` and `txs_type` range types provide standard range-based `for` iteration to callers.

`txs_iter_impl` carries a `metadata_` flag, initialized to `!open()`. Closed ledgers pack each transaction SHAMap item as `addVL(txBytes) || addVL(metaBytes)`, so iteration calls `deserializeTxPlusMeta` and returns both. Open ledgers store only the raw transaction bytes, so `metadata_` is `false` and iteration calls `deserializeTx`, returning a null metadata pointer. This dual-path deserialization is a protocol-level invariant: metadata only exists in a closed ledger.

## Raw Mutation Primitives

`rawInsert`, `rawReplace`, `rawErase`, and `rawTxInsert` are the low-level write interface. They directly manipulate the backing SHAMaps and throw `LogicError` on contract violations (duplicate inserts, missing keys for replace/erase). The `raw` prefix is intentional — callers are responsible for maintaining ledger invariants. These are not guarded against logical errors; they detect programming mistakes in the caller.

`rawTxInsertWithHash` extends `rawTxInsert` by computing and returning the SHA-512 half-hash of the resulting SHAMap leaf node (using `HashPrefix::txNode`, the item data, and the key). This hash directly identifies the transaction's storage location in the tree and is used for efficient transaction proof generation without a full tree traversal.

## Skip List Maintenance

`updateSkipList()` implements a two-tier skip list for fast ledger hash lookup by sequence. It maintains two on-ledger SLEs: a rolling window of the last 256 parent hashes (sliding window, oldest evicted when full), and a sparse list where every 256th ledger stores its hash as a permanent record. This combination enables O(1) lookup for recent ledgers and O(1) lookup at 256-ledger granularity for deep history, supporting the `getLedgerHashForSeq` operation efficiently.

## Negative UNL Support

The negative UNL (nUNL) is a per-ledger list of validators temporarily removed from the effective quorum because they appear offline. `negativeUNL()` reads the `sfDisabledValidators` array from the `keylet::negativeUNL()` SLE and returns validator public keys. `updateNegativeUNL()` applies pending enable/disable transitions: it replaces the disabled list in-place, removes the `sfValidatorToDisable`/`sfValidatorToReEnable` pending fields, and either updates or erases the SLE if the resulting list is empty. Per the header documentation, this must only be called at flag ledgers and before applying `UNLModify` transactions.

## Integrity Verification

`walkLedger()` traverses both SHAMaps from their roots, collecting any missing nodes, and returns `false` if any gaps exist. It supports parallel traversal of the state map (using 32 worker threads via `walkMapParallel`), useful for validation of large ledgers during sync. `isSensible()` performs a quick consistency check — verifying that the header hash roots match the actual SHAMap hashes — and is used as a fast sanity gate before more expensive processing.