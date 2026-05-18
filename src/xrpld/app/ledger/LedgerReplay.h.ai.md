# `LedgerReplay.h` — Replay Data Bundle for Ledger Reconstruction

`LedgerReplay` is a focused, immutable value object that packages everything needed to deterministically re-execute a previously closed ledger: the parent ledger, the target ledger being replayed, and the set of transactions in their canonical application order. It is the single handoff point between the components that *acquire* ledger data and the `buildLedger` function that *applies* it.

## Role in the System

When the XRPL node is missing a historical ledger, the `LedgerReplayer` subsystem can reconstruct it from peers rather than downloading the full SHAMap state. The workflow is:

1. `LedgerDeltaAcquire` fetches the header and ordered transactions from a peer over the `TMReplayDeltaRequest` protocol message.
2. A `LedgerReplay` object is assembled from the parent ledger and the fetched data.
3. `buildLedger(LedgerReplay const&, ...)` in `BuildLedger.cpp` iterates `orderedTxns()` and calls `applyTransaction` for each, recreating the ledger state from scratch. The resulting ledger hash must match the expected hash or the attempt fails.

There is also a secondary usage path through `LedgerMaster::takeReplay` / `releaseReplay`, which holds a `unique_ptr<LedgerReplay>` for manual replay triggering during ledger publication.

## Design of the Two Constructors

The two-constructor design reflects two distinct provenance paths for the transaction set.

The **single-argument constructor** (taking only `parent` and `replay`) auto-derives the ordered transaction map directly from the replay ledger's own `txMap()`. It iterates `replay_->txMap()`, calls `txRead(item.key())` to fetch both the `STTx` and its metadata, then reads `sfTransactionIndex` from the metadata to determine canonical position. Using the metadata's `sfTransactionIndex` field is important: it is the authoritative record of the order in which transactions were applied when the ledger was originally closed, not merely the order in which they arrived or were sorted. This constructor is used when a fully validated `Ledger` object is already available locally.

The **move constructor** accepts a pre-built `std::map<std::uint32_t, std::shared_ptr<STTx const>>&&` and simply moves it in. This path is used by `LedgerDeltaAcquire::tryBuild`, where the ordered transaction list arrives over the network inside a `TMReplayDeltaResponse` message and has already been assembled by the message handler before `LedgerReplay` is constructed. The move avoids an unnecessary copy of what could be a large map.

## Ordering Invariant

The map key type `std::uint32_t` represents `sfTransactionIndex`, a zero-based position field embedded in each transaction's metadata object at the time the ledger was first built. Because `std::map` iterates in ascending key order, any range-for over `orderedTxns()` visits transactions in exactly the sequence they were applied originally. This ordering invariant is load-bearing: applying transactions to an `OpenView` is not commutative — earlier transactions can consume sequence numbers or reserve funds that affect whether later transactions succeed — so any permutation would produce a different `txMap` root hash and a different ledger hash.

## Immutability and Ownership

All three data members are `private`, and all three accessors return `const&`. Once constructed, the object cannot be mutated. Ownership of the underlying `Ledger` objects is shared via `shared_ptr<Ledger const>`; `LedgerReplay` is a co-owner, keeping both ledgers alive for the duration of the replay operation. The object itself is typically stack-allocated at the call site of `buildLedger` and destroyed immediately after the new ledger is built.

## `CountedObject` Instrumentation

Inheriting from `CountedObject<LedgerReplay>` registers the type with the global `CountedObjects` registry at zero cost to normal operation. This allows the server's diagnostic layer to report how many `LedgerReplay` instances exist at any moment — useful for detecting leaks or unexpected accumulation of in-flight replay tasks during debugging or monitoring.