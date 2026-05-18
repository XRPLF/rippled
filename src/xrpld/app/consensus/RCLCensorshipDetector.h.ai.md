# RCLCensorshipDetector.h

## Purpose

`RCLCensorshipDetector` tracks transactions that the local validating node has proposed across multiple consensus rounds, looking for transactions that the network consistently refuses to include in a ledger. Such systematic exclusion — where a transaction is repeatedly eligible but never confirmed — can signal censorship by a bloc of validators. The class provides the data structure and merge logic; the policy for what constitutes a warning and how to surface it lives in the caller (`RCLConsensus`).

## Template Design

The class is templated on `TxID` and `Sequence`, keeping it generic. In practice it is always instantiated as `RCLCensorshipDetector<uint256, LedgerIndex>`, where `LedgerIndex` records which ledger sequence the transaction was *first* proposed in — not the current round. This distinction is essential: by freezing the sequence number at first proposal, the detector can later compute `current_ledger - first_proposed_ledger` to determine exactly how many rounds a transaction has been waiting.

## Internal State

The single data member `tracker_` is a `std::vector<TxIDSeq>` kept in sorted order. `TxIDSeq` pairs a transaction ID with its first-seen ledger sequence. The three `operator<` overloads defined as friends cover all comparison combinations needed by the sorted-range algorithms: `TxIDSeq` vs. `TxIDSeq`, `TxIDSeq` vs. raw `TxID`, and `TxID` vs. `TxIDSeq`. This heterogeneous ordering is what lets `remove_if_intersect_or_match` (which receives the `accepted` set as a plain `vector<TxID>`) operate in a single linear pass against `tracker_` without converting types.

Choosing a sorted vector over a hash map is a deliberate performance tradeoff. Both `propose()` and `check()` call custom merge algorithms from `xrpl/basics/algorithm.h` that exploit sorted order to run in O(n + m) time. A hash map would need individual lookups with worse cache behavior for these bulk operations.

## The propose() Method

`propose()` is called at the beginning of each consensus round with the transactions the node is putting forward. Its job is to reconcile the incoming proposal with whatever was tracked from the previous round:

1. Transactions present in the previous round *and* still being proposed retain their original `seq` (the round they were first seen). The `generalized_set_intersection` call performs this in-place update: for each transaction in the intersection of the new proposal and the old tracker, it copies the stored `seq` from the old entry into the new one.
2. Transactions that were tracked before but dropped from this round's proposal are discarded.
3. Brand-new transactions are added with the current ledger sequence as their `seq`.

The result is that `tracker_` always holds exactly the set of transactions the node is *currently* proposing, with `seq` set to the earliest ledger in which each was first proposed.

## The check() Method

`check()` is called after consensus completes, once the accepted transaction set is known. It removes entries from `tracker_` in two categories:

- **Accepted transactions**: any tracked transaction that made it into the ledger is removed unconditionally.
- **Predicate-matched transactions**: for each tracked transaction still pending, the supplied predicate `pred(TxID, Sequence)` is consulted. If it returns `true`, the entry is removed.

The predicate in `RCLConsensus::buildLCL()` does two things. First, it silently removes transactions that *failed* (bad fee, wrong account state, etc.) — these should not trigger a warning because their exclusion is legitimate, not malicious. Second, for transactions that have legitimately been waiting, it checks whether `(current_seq - first_proposed_seq) % censorshipWarnInternal == 0`, where `censorshipWarnInternal` is 15. This fires a `JLOG(j.warn())` message every 15 ledgers for each persistently unconfirmed transaction, providing periodic escalating visibility without log flooding.

The predicate returns `false` for transactions that should continue to be tracked, keeping them alive in `tracker_` for future rounds.

## The reset() Method

`reset()` clears all state by emptying `tracker_`. It is called from `RCLConsensus::consensusModeChange()` whenever the node transitions away from proposing mode — for example, when it loses sync with the network, reconnects after an outage, or switches from proposing to observing. Without this guard, transactions queued before a disconnect would generate spurious censorship warnings when the node rejoins, since they would appear to have been waiting through the entire outage period.

## Integration in RCLConsensus

`RCLConsensus::Imp` holds a single `censorshipDetector_` member. The two-phase call pattern maps naturally to the consensus lifecycle:

- `propose()` is called inside `RCLConsensus::onConsensusReached()` when the node builds its initial transaction set from the `SHAMap`, visited leaf by leaf.
- `check()` is called inside `RCLConsensus::buildLCL()` after `applyTransactions()` has determined which transactions were accepted and which were retriable or failed.

The class is not thread-safe — it lives entirely within `RCLConsensus::Imp`, which is accessed under the application's consensus lock.