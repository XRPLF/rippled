# `LedgerReplay.cpp` — Transaction Ordering for Ledger Reconstruction

`LedgerReplay.cpp` provides the constructor implementations for the `LedgerReplay` value object — a lightweight data bundle that pairs two ledger snapshots with their transactions sorted into deterministic application order. It is the handoff type between the network acquisition layer and the `buildLedger()` function that re-executes a historical ledger from scratch.

## Why This Class Exists

When an XRPL node is missing a historical ledger, it has two ways to reconstruct it: download the full SHAMap state (expensive), or replay the transactions against the known parent ledger (efficient). The replay path requires two things: the parent ledger to start from, and the exact sequence of transactions to apply. `LedgerReplay` holds both, along with a reference to the target ledger whose metadata drives the reconstruction parameters (close time, close flags, close resolution).

`buildLedger(LedgerReplay const&, ...)` in `BuildLedger.cpp` consumes the object directly — it iterates `orderedTxns()` in map order and calls `applyTransaction()` for each, then finalises the resulting ledger using the replay ledger's own header metadata.

## The Two Constructors

The class offers two construction paths that differ in how `orderedTxns_` is populated.

The **primary constructor** takes only `parent` and `replay` ledgers and builds the ordered transaction map itself. It walks `replay_->txMap()` to enumerate all transaction keys, then for each key calls `replay_->txRead(item.key())`, which returns a `tx_type` — a `std::pair<std::shared_ptr<STTx const>, std::shared_ptr<STObject const>>`. The second element is the transaction metadata `STObject`, which for a closed ledger carries an `sfTransactionIndex` field recording the position at which the transaction was applied. The constructor extracts that index and uses it as the map key, inserting the transaction (moved, not copied, to avoid a shared-pointer refcount bump) into `orderedTxns_`. Because `std::map` orders by key, the resulting container is automatically sorted by application sequence — exactly what `buildLedger()` needs to deterministically reproduce the ledger.

The **secondary constructor** accepts a pre-built `std::map<std::uint32_t, std::shared_ptr<STTx const>>&&` and moves it directly into `orderedTxns_`. This path is used when `LedgerDeltaAcquire` has already assembled the ordered transaction set from peer-fetched data; constructing it inline avoids re-iterating the txMap a second time.

## Design Observations

There is no validation in either constructor — no null checks on `parent_` or `replay_`, no assertion that the metadata contains `sfTransactionIndex`. This is intentional: callers (specifically `LedgerDeltaAcquire` and `LedgerReplayer`) are responsible for verifying that the ledger data is structurally sound before creating a `LedgerReplay`. The class trusts its inputs and acts as a pure data carrier.

`LedgerReplay` inherits from `CountedObject<LedgerReplay>`, which adds lightweight live-instance tracking used for diagnostic reporting — a common pattern across XRPL protocol objects, adding zero per-instance overhead beyond the counter.

All three stored fields are `shared_ptr` types, so the object can be cheaply copied or passed through multiple subsystem layers without duplicating ledger data. The `orderedTxns_` map holding `shared_ptr<STTx const>` nodes similarly allows the same transaction objects to be referenced from both the replay ledger's own txMap and the replay context without duplication.