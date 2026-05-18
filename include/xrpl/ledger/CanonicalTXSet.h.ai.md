# `CanonicalTXSet` — Ordered Transaction Queue for Consensus Application

## Role in the System

`CanonicalTXSet` holds transactions that have been deferred from a previous ledger-building pass and need to be reapplied in the next pass of the consensus process. Its central responsibility is enforcing a *canonical*, deterministic ordering so that every validator that holds the same transaction set produces identical ledger results when applying those transactions. The class lives in `include/xrpl/ledger/CanonicalTXSet.h`, with its implementation in `src/libxrpl/ledger/CanonicalTXSet.cpp`.

The name "canonical" refers specifically to the ordering guarantee: given the same inputs, every node applies transactions in the same sequence. This is a hard requirement for a Byzantine fault-tolerant ledger — divergent apply orders would cause validators to diverge on ledger hashes even when they agree on the transaction set.

## The `Key` Type and its Sorting Logic

The private `Key` struct is the architectural core. Each key holds three fields: a salted account identifier (`account_`), a `SeqProxy` (sequence or ticket), and the transaction hash (`txId_`). The `operator<` implementation in the `.cpp` file reveals the three-level sort:

1. First by salted account ID — grouping all transactions from the same account together.
2. Then by `SeqProxy` within an account — applying sequence-ordered transactions before ticket transactions (a property enforced by `SeqProxy`'s own comparator, which sorts all `seq`-type values before any `ticket`-type values).
3. Finally by transaction ID as a tiebreaker.

The equality operator (`operator==`) behaves differently from the less-than operator: it tests only `txId_`. This asymmetry is intentional. The map key space orders by account-then-sequence for retrieval logic, but identity is solely the transaction hash. This makes it safe to use iterator-based removal (`erase`) without accidentally conflating two different transactions that happen to share account/sequence context.

## Salt: Preventing Position Mining

A subtle but important security measure is account-key salting. The `accountKey()` method doesn't use the raw `AccountID` as the sort key. Instead it XORs the account bytes with a `LedgerHash` salt:

```cpp
ret ^= salt_;  // salt is a LedgerHash set at construction
```

Without salting, an attacker could craft account addresses with intentionally low byte values to ensure their transactions sort early in every ledger's apply order — gaining a persistent ordering advantage. By XORing with the current ledger's hash (which changes every ledger), the effective sort position of any given account is randomized per ledger. The salt is passed at construction time and can be refreshed via `reset()`.

## `SeqProxy` Integration

`SeqProxy` is a tagged union of a 32-bit sequence number and a 32-bit ticket number. Its comparator ensures sequence-based transactions always sort before ticket-based ones, regardless of numeric value. This ordering guarantee flows directly into `CanonicalTXSet`: for any given account, all regular-sequence transactions are attempted before any ticket-based transactions. This matters because ticket-creating transactions (which use a sequence) must be applied before ticket-consuming ones — the sort order enforces that dependency automatically.

## `popAcctTransaction()`: Chaining Transactions from the Same Account

After a transaction from a given account is successfully applied to the open ledger, the caller invokes `popAcctTransaction()` to retrieve and remove the next eligible transaction from that same account. The method uses `lower_bound` with a key whose `txId_` is `beast::zero` (sorting before any real transaction ID) to locate the first key greater than the just-applied transaction's position. It then returns that next transaction only if it belongs to the same account *and* satisfies one of:

- It uses a ticket (tickets can be applied regardless of sequence gaps).
- Its sequence value is exactly one more than the current transaction's sequence.

This prevents accidentally applying a transaction whose predecessor hasn't yet been processed, which would violate the XRPL account sequence invariant. When there is no suitable successor, `popAcctTransaction()` returns `nullptr`.

## Usage in `applyTransactions()`

In `BuildLedger.cpp`, `applyTransactions()` iterates the `CanonicalTXSet` in sorted order across multiple passes. Successful and definitively-failed transactions are erased; retryable ones remain for subsequent passes. Because the map's iteration order is the canonical order, every validator performs the same iteration. The `key()` accessor exposes the salt hash for logging purposes — the build log records the transaction set identity alongside the close time.

## Lifecycle and Memory Accounting

The class inherits from `CountedObject<CanonicalTXSet>`, which increments a global atomic counter on construction and decrements it on destruction. This is a diagnostic utility: the counts are queryable via `CountedObjects::getInstance().getCounts()` for memory-pressure reporting. It has no impact on functional behavior.

`reset()` allows the same `CanonicalTXSet` instance to be reused across ledger rounds by installing a new salt and clearing the map — avoiding repeated heap allocations for the container itself.