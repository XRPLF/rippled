# `TxQ.h` — Transaction Queue and Fee Escalation Engine

## Role in the System

`TxQ.h` defines the `TxQ` class, which is the core of XRPL's fee escalation and transaction queuing subsystem. Its fundamental purpose is to ensure the network remains usable under both normal and adversarial load conditions. When the open ledger fills up and the required fee rises too high for a transaction to enter immediately, `TxQ` holds that transaction in an ordered backlog so it can be applied to the *next* open ledger. This prevents legitimate low-fee transactions from being silently dropped while simultaneously making flooding attacks prohibitively expensive.

## The Fee Escalation Model

The escalation formula that drives the entire system is embedded in `FeeMetrics::scaleFeeLevel()`:

```
requiredFeeLevel = escalationMultiplier × (txCountInLedger²) / (txnsExpected²)
```

When the open ledger count is below `txnsExpected`, the required level stays at `baseLevel` (256, the minimum for a single-signed reference transaction). Once the count crosses that threshold, the required fee grows quadratically — by the time `txCountInLedger` doubles `txnsExpected`, the required level is four times the multiplier, which itself starts at 500× base. The result is a fee cliff that quickly becomes economically intractable for attackers while allowing legitimate users to pay a higher fee for priority access.

The `escalationMultiplier` itself adapts after every closed ledger: `FeeMetrics::update()` computes the median fee level of all transactions in the validated ledger. Using the median — not the mean or the maximum — keeps the multiplier honest; a handful of high-fee transactions by a single user cannot inflate it unfairly. If consensus is slow (`timeLeap == true`), the target `txnsExpected_` is cut by `slowConsensusDecreasePercent` (default 50%), causing the escalation curve to steepen faster the next round. Under healthy consensus it grows by `normalConsensusIncreasePercent` (default 20%) when the ledger is over capacity.

## Dual Index: `byFee_` and `byAccount_`

The queue maintains two indexes simultaneously. `byFee_` is a `boost::intrusive::multiset<MaybeTx>` ordered by `OrderCandidates`, giving the queue its priority-queue behaviour. `byAccount_` is a `std::map<AccountID, TxQAccount>`, where each `TxQAccount` holds a `std::map<SeqProxy, MaybeTx>` for per-sequence ordering.

The choice of `boost::intrusive` is deliberate and architecturally important: a `MaybeTx` object lives in exactly one memory location and is simultaneously a node in both indexes via `byFeeListHook`. There are no copies and no heap-allocated pointers between the structures. This makes insertion and removal `O(log n)` with no additional allocation, which matters when many transactions are in flight during a burst.

## `MaybeTx` and Its Invariants

`MaybeTx` wraps a queued transaction and carries all the state needed to re-apply it later. The most important field is `pfResult`, an `std::optional<PreflightResult const>`. The comment on this field states the invariant clearly: it is *never* allowed to be empty; the `std::optional` wrapper exists purely to allow in-place construction via `emplace` without a copy assignment. `preflight` is expensive, so the result is cached and reused across subsequent apply attempts unless the ledger rules change.

The retry budget is `retriesAllowed = 10`. A transaction at the front of the queue that returns a `ter` (retriable error) from the transactor is left in the queue and given up to 10 more attempts. Once exhausted it is dropped. This prevents the queue from being wedged by transactions that will never succeed. Two per-account penalty flags in `TxQAccount` reinforce this: `retryPenalty` (reduced to 2 retries for all account transactions after one is dropped for excessive retries) and `dropPenalty` (causes account transactions to be discarded first when the queue is nearly full).

## Pseudo-Random Ordering Within a Fee Level

`OrderCandidates::operator()` sorts `MaybeTx` by descending fee level, and — when two entries share the same fee level — by `txID ^ MaybeTx::parentHashComp` ascending. `parentHashComp` is a static field set to the parent ledger hash when a new ledger opens. This XOR produces a deterministic but unpredictable ordering that is *identical* across all validators seeing the same ledger. The design goal is for validators to build nearly identical queues after consensus, which increases the probability that their initial transaction proposals overlap and consensus converges quickly. A plain sort by `txID` would also be deterministic but potentially exploitable; XOR with the parent hash prevents submitters from crafting a transaction ID that always wins within a fee tier.

## Lifecycle Methods

Three methods correspond to the three phases of XRPL's ledger cycle:

- **`apply()`**: Called when a new transaction arrives. It first tries to apply the transaction directly to the open ledger via `tryDirectApply()`. If the fee is too low for the open ledger but acceptable for the queue, it calls `canBeHeld()` to check per-account limits, `LastLedgerSequence` constraints, and whether dependent transactions can still be paid. Returns `{ terQUEUED, false }` on successful queuing. The special path `tryClearAccountQueueUpThruTx()` handles the case where a new high-fee transaction for an account can pay enough to flush the entire pre-existing queue chain for that account in one shot — an "all-or-nothing" atomic commitment that avoids partial queue drain.

- **`accept()`**: Called at the start of each new open ledger, after the previous ledger closes. It drains the queue from highest fee level down, applying each `MaybeTx` to the open ledger via a sandbox. The loop stops as soon as the required fee level rises above the next candidate's level. The `eraseAndAdvance()` method implements a subtle ordering: it prefers the *next transaction for the same account* (if it has a higher or equal fee) over the globally next-in-fee entry, preserving sequence ordering for an account's chain of transactions.

- **`processClosedLedger()`**: Called when a ledger is validated. It invokes `FeeMetrics::update()` to recompute the escalation multiplier and `txnsExpected_`, updates `maxSize_` (the dynamic queue size cap based on `ledgersInQueue × recentLedgerSize`), and evicts any entry whose `LastLedgerSequence` has expired.

## Thread Safety

All mutable state (`feeMetrics_`, `byFee_`, `byAccount_`, `maxSize_`) is protected by `mutex_`. The internal `nextQueuableSeqImpl()` takes a `std::lock_guard<std::mutex> const&` by value, forcing callers to hold the lock — a compile-time enforcement of the locking discipline rather than a runtime assertion. The public `nextQueuableSeq()` acquires the lock itself, while the private implementation is reused by other locked callers. A comment notes that most queue operations happen under the app-wide master lock, but `mutex_` exists specifically for the `fee` RPC command, which does not hold the master lock.

## Fee Unit Helpers

The free functions `toDrops()` and `toFeeLevel()` convert between raw XRP drops and normalized fee levels using `mulDiv` to avoid floating point. Both use saturating arithmetic: on overflow `toDrops()` returns `STAmount::cMaxNativeN` (the maximum representable native amount) and `toFeeLevel()` returns `UINT64_MAX`, ensuring callers never see a fee that appears artificially cheap due to integer wrap.

## Configuration via `Setup`

`setup_TxQ(Config const&)` constructs a `TxQ::Setup` from the application configuration file. The defaults embedded in `Setup` encode the experimentally chosen constants described in `FeeEscalation.md`: `ledgersInQueue = 20`, `queueSizeMin = 2000`, `retrySequencePercent = 25`, `maximumTxnPerAccount = 10`, and `minimumLastLedgerBuffer = 2`. The standalone mode override (`minimumTxnInLedgerSA = 1000`) ensures unit tests do not have to saturate the queue before testing fee escalation behaviour.