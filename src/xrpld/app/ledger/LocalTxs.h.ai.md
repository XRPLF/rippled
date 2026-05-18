# `LocalTxs.h` — Local Transaction Retention Interface

## Role in the System

`LocalTxs.h` defines the abstract interface that guards against a subtle consensus-divergence hazard: a transaction submitted by a local client can be silently dropped from the node's open ledger when that node's consensus view diverges from the network majority. Without intervention, the client's second transaction would receive `terPRE_SEQ` (because the node no longer believes the first transaction was applied), and the node would not relay it further.

The interface contracts a small buffer — by design holding pending local transactions for up to five ledgers — and re-applies them to every new open ledger until a fully-validated ledger confirms they were accepted, expired, or became impossible. The `holdLedgers = 5` constant is intentionally generous: five ledger closes is ample time for a transaction to propagate through consensus under normal network conditions, while the `sfLastLedgerSequence` field on the transaction itself can tighten the window if the sender requires faster expiry.

## Interface Design

`LocalTxs` is a pure virtual base with four operations and a factory function:

```
virtual void push_back(LedgerIndex index, shared_ptr<STTx const> const& txn) = 0;
virtual CanonicalTXSet getTxSet() = 0;
virtual void sweep(ReadView const& view) = 0;
virtual size_t size() = 0;

unique_ptr<LocalTxs> make_LocalTxs();
```

Hiding the implementation behind a pure interface (with `make_LocalTxs()` as its factory) keeps the concrete class, its mutex, and its internal `LocalTx` wrapper type entirely out of the header. Callers — notably `NetworkOPsImp` and `RCLConsensus::Adaptor` — depend only on this narrow contract, which simplifies testing and prevents accidental coupling to implementation details.

## Lifecycle and Callers

`NetworkOPsImp` owns the single live instance as a `std::unique_ptr<LocalTxs> m_localTX`, constructed via `make_LocalTxs()` at startup.

**Enrollment (`push_back`):** When the node's transaction-processing path applies a transaction flagged as `local`, and the transaction is not forced-hard-failed, `NetworkOPsImp` calls `push_back` with the current open ledger index and the signed transaction. This enrolls the transaction in the retention buffer so it survives any subsequent consensus rollback.

**Re-application (`getTxSet`):** Both `NetworkOPsImp::doAdvance` and `RCLConsensus::Adaptor` call `getTxSet` when constructing a new open ledger. The method packages all currently held transactions into a `CanonicalTXSet` (initialized with a zero salt) so they are applied in a deterministic per-account sequence order. Using `CanonicalTXSet` here ensures transactions from the same account are ordered by `SeqProxy`, preventing the engine from rejecting them for out-of-order application.

**Pruning (`sweep`):** `LedgerMaster::setValidLedger` calls `app_.getOPs().updateLocalTx(*l)`, which delegates to `m_localTX->sweep(view)`, each time a new fully-validated ledger arrives. The concrete `sweep` implementation runs `std::list::remove_if` with a lambda that queries the validated ledger's `ReadView` to determine which held transactions are no longer needed. Three pruning conditions apply in order:

1. **Expiry by ledger index.** If the current validated ledger sequence exceeds the transaction's expiration (the minimum of the enrollment ledger plus `holdLedgers` and `sfLastLedgerSequence + 1`), the entry is dropped unconditionally.
2. **Confirmed inclusion.** If `view.txExists(id)` returns true, the validated ledger already contains the transaction; it is removed.
3. **Account-sequence analysis.** For sequence-based transactions (`seqProx.isSeq()`), if the account's on-ledger sequence has advanced past the transaction's sequence number, the transaction would produce `tefPAST_SEQ` and is discarded. For ticket-based transactions, the code keeps entries whose ticket has not yet been created (the account sequence hasn't reached the ticket number yet), but removes them once the ticket should exist and `keylet::ticket` can be confirmed absent from the ledger.

This three-stage sweep avoids prematurely evicting ticket transactions that are genuinely waiting for their ticket to be created, while still bounding their lifetime by the `holdLedgers` ceiling.

## Concurrency

The concrete implementation (`LocalTxsImp`) stores entries in a `std::list<LocalTx>` protected by a `std::mutex`. All four public methods acquire the lock under a `std::lock_guard`. Transactions can be submitted concurrently from client-facing threads while consensus and ledger-advance threads call `getTxSet` and `sweep`, making the lock mandatory. The `std::list` is chosen deliberately: `remove_if` invalidates only erased iterators, and the list never moves elements, so no iterator re-validation is needed after incremental removal.

## Key Invariant

The interface enforces the invariant that no local transaction remains in the buffer for more than `holdLedgers` validated ledger closes, regardless of network conditions, ticket state, or whether the transaction was ultimately applied. This bounds memory consumption and prevents stale transactions from accumulating during extended network partitions.