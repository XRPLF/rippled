# `CanonicalTXSet.cpp` — Ordered Transaction Queue for Consensus Retry

`CanonicalTXSet` is the data structure that holds transactions which could not be applied during one pass of consensus and need to be retried on the next. The word "canonical" refers not to uniqueness but to the deterministic, agreed-upon order in which every validating node will process these transactions — a prerequisite for reaching identical ledger states across the network.

## The Core Sorting Problem

When the network closes a ledger, it applies a set of agreed-upon transactions. Some transactions fail on the first attempt — often because a prerequisite transaction from the same account hasn't been applied yet — and these are deferred into a `CanonicalTXSet` for retry. The critical invariant is that every node must iterate through these deferred transactions in the same order, so the struct is built around a `std::map` sorted by a carefully designed three-part composite key.

## The `Key` Structure and Its Ordering

The `Key` type encodes three fields: a salted 256-bit account identifier, a `SeqProxy` (which is either a sequence number or a ticket number), and the transaction hash as a tiebreaker. The `operator<` in `CanonicalTXSet.cpp` makes the ordering explicit:

```
account_ → seqProxy_ → txId_
```

This groups all transactions from the same account together, then sorts them within that group by `SeqProxy`. The tie on `txId_` is just for determinism when two transactions share an account and a `SeqProxy` value (an edge case that should not arise in valid traffic, but the key must still define a strict weak order).

The `SeqProxy` comparison itself (defined in `SeqProxy.h`) has a notable property: sequences always sort before tickets regardless of their numeric values. A `SeqProxy` of type `seq` with value 1000 sorts before a `SeqProxy` of type `ticket` with value 1. This is intentional — `TicketCreate` transactions (which use sequences) must be applied before any transaction that consumes one of those tickets. The sort order enforces this dependency mechanically.

## The Salt: Anti-Mining Protection

`accountKey()` performs a small but important transformation:

```cpp
uint256 ret = beast::zero;
memcpy(ret.begin(), account.begin(), account.size());
ret ^= salt_;
return ret;
```

The `AccountID` (a 20-byte hash) is embedded in a 256-bit zero-initialized value and XORed with `salt_`, which is derived from the `LedgerHash` of the just-closed ledger. Without this salt, an attacker could deliberately mine an `AccountID` with a numerically low value, guaranteeing their transactions are processed first in every ledger forever. By rotating the salt each ledger, the account ordering is unpredictable from ledger to ledger, making such an attack economically infeasible.

The salt is passed in at construction time and can be refreshed via `reset(LedgerHash const&)`, which also clears the map. This is how the set is recycled between consensus rounds.

## `insert()` — Straightforward Key Construction

`insert()` is thin: it constructs a `Key` from the salted account, the transaction's `SeqProxy` (sequence or ticket), and the transaction hash, then inserts the `(Key, shared_ptr<STTx>)` pair into `map_`. There is no deduplication guard here; callers are expected to avoid inserting the same transaction twice. Validation that the transaction is well-formed has already happened upstream.

## `popAcctTransaction()` — The Most Complex Logic

This is the function the ledger-building code calls after successfully applying a transaction from the set. Given the transaction just applied, it looks for the next eligible transaction from the same account and removes it from the map.

The lookup uses `map_.lower_bound(after)` where `after` is a key constructed with `txId_ = beast::zero`. Since `zero` is the smallest possible `uint256`, this finds the first key strictly after the current position for this account and `SeqProxy`. The code then checks two conditions on the candidate:

1. **Same account** — the iterator must still be in the same account's slot. If the candidate belongs to a different account, there is no next transaction to return.
2. **Valid successor** — the next transaction is eligible if it uses a ticket (tickets can be applied in any order) *or* if its sequence number is exactly `seqProxy.value() + 1` (sequences must be consecutive). This models the XRPL rule that sequence-based transactions form a chain; you cannot apply sequence 5 if 4 has not been applied yet.

If both conditions are met, the transaction is moved out of the map and returned to the caller, which can immediately attempt to apply it. If either condition fails, `nullptr` is returned and the caller knows there is no immediately eligible follow-up for this account.

## Usage in `BuildLedger.cpp`

`applyTransactions()` in `BuildLedger.cpp` iterates the `CanonicalTXSet` linearly across multiple passes (up to `LEDGER_TOTAL_PASSES`). On each pass it calls `applyTransaction()` for each entry and erases successes and definitive failures, leaving only retryable transactions for the next pass. `popAcctTransaction()` is used by the open-ledger path (`OpenLedger`) to chain an account's transactions without waiting for an explicit retry sweep.

## Invariants and Design Tradeoffs

The design trusts that transactions inserted into the set are already validated — there is no re-checking of signatures or format inside `CanonicalTXSet`. Ownership of the `STTx` objects is shared via `std::shared_ptr`, so the map holds a reference without taking exclusive ownership; the same transaction object may be referenced from multiple places while retry is in progress.

`equality` on `Key` is defined solely on `txId_`, not on the full composite key. This means two `Key`s with the same transaction hash but different account/seqProxy values compare equal. In practice this should never occur (the transaction hash is derived from the full transaction content which includes the account and sequence), but the asymmetry between `operator==` and `operator<` is worth noting when reading the code.