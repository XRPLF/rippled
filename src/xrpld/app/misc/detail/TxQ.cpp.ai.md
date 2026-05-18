# `src/xrpld/app/misc/detail/TxQ.cpp`

## Role and Purpose

This file is the implementation of XRPL's transaction queue — the mechanism that absorbs transactions when the open ledger is too full (or their fee too low) to accept them immediately. It works in concert with fee escalation: once the open ledger passes a target transaction count, the required fee rises quadratically, and any transaction that cannot clear that bar is held here and retried when the next open ledger opens. The design goal is to simultaneously protect validators from spam, provide fair ordering for legitimate high-volume senders, and give transactions a second chance at inclusion rather than dropping them outright.

The file is purely an implementation detail; the public interface lives in `TxQ.h`.

---

## Fee Level Utilities

Three small file-scope helpers establish the vocabulary used throughout:

`getFeeLevelPaid()` translates a raw XRP fee into a normalized `FeeLevel64`, where `TxQ::baseLevel` (256) represents the reference cost of a single-signed transaction. The normalization is `mulDiv(feePaid, 256, baseFee)`. The interesting edge case is when `calculateBaseFee` returns zero — something possible on networks where ordinary transactions are free. In that case a small `mod` value (the default base fee, or 1 drop as a last resort) is added to both numerator and denominator so that the comparison remains meaningful. `mulDiv` itself is overflow-safe and returns `std::optional`; the call site resolves overflow to `UINT64_MAX`, which ensures an overflowing fee is treated as extremely high rather than zero.

`getLastLedgerSequence()` is a thin wrapper that returns `std::nullopt` if the field is absent, sparing callers from presence checks.

`increase()` multiplies a fee level by `(100 + increasePercent) / 100`, used when computing the minimum fee bump required to replace a queued transaction (default 25%).

---

## FeeMetrics

`FeeMetrics` tracks two adaptive scalars that drive the escalation curve: `txnsExpected_` (the per-ledger transaction budget) and `escalationMultiplier_` (derived from the median fee of the last closed ledger).

### `FeeMetrics::update()` — Adaptive Budget

Called once per closed ledger via `processClosedLedger()`. It iterates every transaction in the validated ledger, computes its fee level, sorts the resulting vector, and takes the median as the new multiplier. The update to `txnsExpected_` uses two modes:

- **Slow consensus** (`timeLeap == true`): Immediately cuts `txnsExpected_` by `slowConsensusDecreasePercent` (default 50%), bounded below by `minimumTxnCount_`. The circular buffer of recent high-traffic counts is also wiped, preventing a prior spike from inflating the budget during a slow period.
- **Normal consensus**: Pushes a `normalConsensusIncreasePercent`-bumped version of the actual count into a circular buffer and takes the max. Growth is fast (the max element of recent history), while shrinkage is slow: if the max element is already below `txnsExpected_`, the new value is only 10% of the way toward that max, providing hysteresis.

### `FeeMetrics::scaleFeeLevel()` — Quadratic Escalation

Below `txnsExpected_`, the required fee stays at `baseLevel` (flat). Once the open ledger exceeds `txnsExpected_`, the formula is:

```
required = multiplier * current² / target²
```

This is quadratic, not linear, which matters: each additional transaction past the threshold raises the bar significantly. With a multiplier of `baseLevel * 500` (the default minimum), a ledger at 2× capacity already requires 2,000× the reference fee. This makes targeted flooding extremely expensive without penalizing normal traffic that stays under the budget.

### `FeeMetrics::escalatedSeriesFeeLevel()` — Fee Averaging for Series

Given a range of transactions queued for an account plus an incoming high-fee transaction, this calculates the total fee requirement for all of them together. It uses the closed-form sum of squares `x(x+1)(2x+1)/6`, implemented in `detail::sumOfFirstSquares()` with an overflow guard at x ≥ 2²¹. The sum covers the ledger slots from `current` (the slot the first queued transaction would occupy) to `current + seriesSize - 1`. Static `assert` statements at the bottom of the `detail` namespace serve as embedded unit tests for the formula.

---

## `MaybeTx` — A Queued Transaction

`MaybeTx` stores a shared pointer to the immutable `STTx`, its computed fee level, `SeqProxy`, preflight result, and a `retriesRemaining` counter (default 10). The static member `parentHashComp` is set each cycle to the open ledger's parent hash; it is used by the intrusive multiset comparator to break fee-level ties, giving pseudo-random but deterministic ordering across validators that see the same ledger.

`MaybeTx::apply()` handles rule changes across open-ledger boundaries by re-running `preflight` if the ledger rules or apply flags have changed since the transaction was first queued.

---

## Dual-Index Storage

The queue maintains two complementary views:

- **`byFee_`**: A Boost intrusive multiset ordered by `(feeLevel DESC, parentHash XOR txID)`. Intrusive containers are used because `MaybeTx` objects live in `TxQAccount::TxMap`; the intrusive hooks are embedded directly in those objects, avoiding secondary heap allocations. The XOR with the parent hash is the determinism mechanism: every node re-evaluating `accept()` will sort equal-fee transactions the same way.
- **`byAccount_`**: An `std::map<AccountID, TxQAccount>` where each `TxQAccount` holds a `std::map<SeqProxy, MaybeTx>` (`TxMap`). The `SeqProxy` ordering ensures sequence-based transactions are processed in strict order while ticket-based transactions can appear anywhere in the map.

Because `byFee_` holds pointers into `TxMap` nodes, erasing from one structure requires erasing from the other in the same critical section. The `erase()` and `eraseAndAdvance()` methods enforce this invariant. `eraseAndAdvance()` additionally implements the "appropriate next candidate" rule: after removing a sequence-based transaction, if the account's next transaction has a higher fee level than the global queue's next candidate, it jumps directly to the account-next, avoiding unnecessary backtracking.

---

## `TxQ::apply()` — The Submission Gate

The main public entry point is the most complex path:

1. **Preflight** validates transaction structure without touching the ledger.
2. **`tryDirectApply()`** fast-paths transactions whose sequence matches the account root and whose fee clears the escalation threshold — they go straight into the ledger, bypassing the queue. If a queued transaction with the same `SeqProxy` exists, it is removed.
3. **Account and ticket checks**: the account must exist; if using a ticket, the ticket SLE must already be in the ledger (not merely queued to be created — this prevents dependency chains that could deadlock).
4. **Blocker rules**: transactions like `SetRegularKey` that invalidate subsequent transactions are allowed only when the account queue is empty or they replace the single existing entry.
5. **Replacement detection**: if the same `SeqProxy` is already queued, the incoming transaction must pay at least 25% more (configurable via `retrySequencePercent`) or it is rejected.
6. **`MultiTxn` sandbox**: when there are existing queued transactions for the account, a shadow `ApplyViewImpl` is constructed that pre-deducts the fees and potential spend of all prior queued transactions from the account's balance. `preclaim` then runs against this adjusted view, catching cases where the account cannot afford the candidate on top of its existing commitments.
7. **`tryClearAccountQueueUpThruTx()`**: if the candidate pays an escalated fee, this attempts to apply all queued transactions ahead of it plus itself in a sandbox. If successful, all queued transactions are erased and the result propagates. This is the fee-averaging path that lets a single high-fee transaction unblock an account queue.
8. **`canBeHeld()`**: enforces `LastLedgerSequence` buffer, per-account queue limit (default 10), and sequence gap rules.
9. **Queue eviction**: if the queue is full and the candidate outbids the account with the lowest average fee level, that account's last transaction is evicted to make room.
10. Finally, the transaction is added to both `byAccount_` and `byFee_`, returning `terQUEUED`.

---

## `processClosedLedger()` and `accept()`

`processClosedLedger()` runs after a ledger closes. It updates fee metrics, recomputes `maxSize_` as `max(txnsExpected * ledgersInQueue, queueSizeMin)` (but only on timely ledgers — slow ledgers do not expand capacity), and sweeps out expired transactions (setting `dropPenalty` on the account).

`accept()` drains the queue into the new open ledger. It iterates `byFee_` from highest to lowest, skipping sequence-based transactions that aren't the head of their account's queue. It applies candidates until the fee threshold is exceeded. Failed transactions with `tef` or `tem` results, or an exhausted retry count, are evicted. Temporary failures (`ter`) decrement the retry counter and leave the transaction in place. A pressure relief valve triggers near 95% queue capacity: accounts with a `dropPenalty` that still have multiple queued transactions have their last transaction evicted proactively.

After draining, `byFee_` is completely cleared and rebuilt from `byAccount_` with the new parent hash. The code comments note this was the fastest method benchmarked versus in-place re-sort or incremental migration.

---

## Concurrency Model

A single `std::mutex mutex_` serializes all queue mutations. `tryDirectApply()` computes the fee comparison before acquiring the lock, acquiring it only in the success path to remove any superseded queued transaction. `nextQueuableSeqImpl()` takes the lock by reference rather than acquiring it independently, a pattern repeated throughout for re-entrant callers that already hold the lock.

---

## `setup_TxQ()`

Parses the `[transaction_queue]` section of the node configuration. The only validation beyond type parsing is that `maximum_txn_in_ledger` may not be lower than `minimum_txn_in_ledger` or `minimum_txn_in_ledger_standalone` — violating this throws `std::runtime_error` at startup, preventing a silently broken configuration from reaching production.