# `src/xrpld/app/ledger/detail/LocalTxs.cpp`

## Purpose

This file solves a specific and subtle failure mode in XRPL consensus: a locally-submitted transaction can silently disappear when the server's view of consensus diverges from the network majority. The file-level comment tells the story in seven steps — a client submits a transaction, the local server believes it was included in its consensus ledger, but the majority ledger wins without that transaction. The server rebuilds an open ledger that has neither the transaction nor a predecessor to it, so a follow-up transaction from the same account fails with `terPRE_SEQ` before it can even be relayed.

The fix is to hold locally-submitted transactions in a short-lived buffer and re-apply them to every new open ledger until they appear in a fully-validated ledger or expire. This is `LocalTxs`.

---

## Architecture: Two Private Classes, One Factory

The public interface is declared in `LocalTxs.h` as a pure abstract base class. The implementation lives entirely within this `.cpp` file, split into two private classes: `LocalTx` (the per-transaction record) and `LocalTxsImp` (the container). The factory function `make_LocalTxs()` returns a heap-allocated `LocalTxsImp` through the abstract interface. This encapsulation means callers never depend on the concrete type — `NetworkOPsImp` stores a `std::unique_ptr<LocalTxs>` and `RCLConsensus::Adaptor` holds a `LocalTxs&` reference.

---

## `LocalTx`: Cached Transaction Wrapper

`LocalTx` wraps a `std::shared_ptr<STTx const>` along with data extracted at construction time: the transaction ID (`m_id`), the submitting account (`m_account`), the sequence/ticket proxy (`m_seqProxy`), and a computed expiry ledger index (`m_expire`).

The expiry logic is meaningful. The default expiry is `index + LocalTxs::holdLedgers` (currently 5 ledgers, a constant on the base class). If the transaction itself carries an `sfLastLedgerSequence` field, the expiry is tightened to `min(default, sfLastLedgerSequence + 1)` — there is no point holding a transaction past its own declared deadline. This means the in-memory hold time mirrors the transaction's own validity window, preventing stale transactions from polluting every new open ledger indefinitely.

Caching the account ID and `SeqProxy` at construction time avoids repeated field lookups inside `sweep()`, which iterates the full list on every validated ledger.

---

## `LocalTxsImp`: Thread-Safe List

The container is a `std::list<LocalTx>` protected by a `std::mutex`. All four public methods — `push_back`, `getTxSet`, `sweep`, and `size` — acquire the mutex with `std::lock_guard`. Using `std::list` rather than `std::vector` is intentional: `sweep()` calls `std::list::remove_if`, which deletes matching elements in a single pass without invalidating iterators on unaffected nodes or requiring a shift of remaining elements.

### `push_back`

Called by `NetworkOPsImp` after a locally-submitted transaction passes initial validation and is applied to the open ledger. The current ledger index is passed as the `index` anchor for computing expiry.

### `getTxSet`

Returns a `CanonicalTXSet` constructed from all currently-held transactions. `CanonicalTXSet` sorts transactions per-account by `SeqProxy`, which ensures that when the consensus engine applies local transactions to a new open ledger they are presented in a valid ordering — sequence numbers in ascending order, tickets ordered correctly among them. The set is built under the lock and then returned by value; the caller (ultimately `NetworkOPsImp::doAdvance` and the `RCLConsensus` adaptor building a new open ledger) applies it outside the lock.

### `sweep`

This is the most complex method. It is called by `NetworkOPsImp::updateLocalTx` after a new ledger is fully validated. It removes entries that are no longer needed, using `remove_if` with a lambda that consults the validated ledger's `ReadView`.

Three removal conditions are checked in order:

1. **Expiry**: if `view.header().seq > m_expire`, the transaction is beyond its hold window and dropped regardless of other state.
2. **Already applied**: `view.txExists(txn.getID())` — the transaction appears in the validated ledger, so it succeeded on some path and should no longer be replayed.
3. **Sequence/ticket invalidation**: this branch runs only if the account exists in the validated ledger (a missing account means the transaction might still be the account-creating transaction, so it is kept).

For sequence-based transactions (`seqProx.isSeq()`): the transaction is dropped if `acctSeq > seqProx`, meaning the account's on-ledger sequence number has already advanced past this transaction's sequence — either the transaction was applied through a different mechanism or a conflicting transaction consumed that slot.

For ticket-based transactions (`seqProx.isTicket()`): the logic is more nuanced. If the account's current sequence is still below or equal to the ticket's value, the ticket hasn't been created yet — this is treated as a "future ticket" and the transaction is kept (the comment notes this hold is still bounded by `m_expire`). If the account sequence has surpassed the ticket value, the ticket should already exist on-ledger; if `keylet::ticket(acctID, seqProx)` does not resolve in the view, the ticket was consumed or never created, and the transaction is removed.

---

## Integration Points

`NetworkOPsImp` owns the `LocalTxs` instance (`m_localTX`) and coordinates all three lifecycle operations:
- **`push_back`** is called in the transaction application path when a transaction is flagged as local and passes the hold criteria.
- **`getTxSet`** is called during `doAdvance` to obtain the retry set when building a new open ledger after consensus.
- **`sweep`** is called via `updateLocalTx` each time a fully-validated ledger arrives.

`RCLConsensus::Adaptor` also calls `localTxs_.getTxSet()` during open ledger construction after a consensus round completes, ensuring local transactions are folded back into the ledger even during normal consensus operation.

---

## Design Tradeoffs

The 5-ledger hold window (`holdLedgers = 5`) is explicitly documented in the header as "essentially arbitrary" — large enough to survive a couple of consensus rounds but small enough not to accumulate stale work indefinitely. The `sfLastLedgerSequence` tightening prevents the buffer from holding transactions longer than the submitter intended.

The use of `std::list` over `std::deque` or `std::vector` prioritizes stable in-place deletion during `sweep` over cache-friendly iteration. Given that this list is expected to be small (locally-submitted transactions from a single server instance), the allocation overhead of `std::list` is acceptable.

No exceptions are thrown anywhere in this file. All error handling is via silent removal: a transaction that cannot be validated is simply not held, and a transaction that proves impossible is swept away on the next validated ledger boundary.