# `TicketCreate.h` — Ticket Batch Creation Transactor

`TicketCreate` is the transactor responsible for minting one or more `Ticket` ledger objects in a single XRPL transaction. Tickets are placeholder sequence-number reservations that allow account owners to submit future transactions out-of-order or in parallel, bypassing the strict account sequence increment that governs normal transaction ordering.

## Role in the Transactor Framework

`TicketCreate` inherits from `Transactor` and participates in the standard three-phase processing pipeline: `preflight` → `preclaim` → `doApply`. Because it must express a non-standard relationship between a single transaction and multiple consumed sequence numbers, it declares `ConsequencesFactory{Custom}`, which signals the framework to invoke `makeTxConsequences` rather than deriving consequences from the default Normal or Blocker rules.

## Calibrated Limits as `constexpr` Constants

The header embeds three `constexpr` values that function as protocol-level policy:

- **`minValidCount = 1`**: A TicketCreate must create at least one ticket; zero is nonsensical and rejected.
- **`maxValidCount = 250`**: The upper bound per transaction. The comment explains the empirical basis: on a MacBook Pro release build with assertions disabled, creating 250 tickets in `doApply()` averaged 1.21 ms, matching the 1.25 ms average for a compute-intensive three-path Payment. The cap exists to keep a single TicketCreate from consuming more validator CPU than any other class of transaction. Encoding this benchmark in a comment next to the constant is deliberate — it makes future reviewers aware of the performance contract being enforced.
- **`maxTicketThreshold = 250`**: An account-level ceiling on total held tickets, chosen to prevent ledger stuffing. This is a per-account invariant enforced during `preclaim`.

These constants are `constexpr` rather than runtime parameters so that the compiler can use them in static assertions and the values are embedded in the binary without any runtime lookup.

## Phase-by-Phase Design

**`makeTxConsequences`** constructs a `TxConsequences` object using the transaction's `sfTicketCount` field. This is critical: the consequences object informs the transaction ordering machinery how many future sequence numbers this transaction "claims," enabling the open-ledger queue to correctly evaluate whether a later transaction referencing one of those ticket sequences can be ordered after this one.

**`preflight`** performs cheap, stateless validation: it reads `sfTicketCount` from the transaction and returns `temINVALID_COUNT` if the value falls outside `[minValidCount, maxValidCount]`. No ledger state is touched here — this is intentional, as preflight runs without a write lock and may be called speculatively.

**`preclaim`** introduces state-aware validation against the read-only ledger view. It computes the net ticket delta:

```cpp
curTicketCount + addedTickets - consumedTickets > maxTicketThreshold
```

The `consumedTickets` term accounts for a subtle case: if the TicketCreate itself was submitted *using* a ticket (i.e., `getSeqProxy().isTicket()` is true), one existing ticket is consumed in the process, so the net increase to the account's ticket inventory is `addedTickets - 1` rather than `addedTickets`. Unsigned integer underflow is explicitly ruled out by the relationship between the three terms, which the comment annotates. Violation returns `tecDIR_FULL`.

**`doApply`** carries out the actual mutation. Key design choices:

1. **Reserve check against `preFeeBalance_`**: The balance check uses the *pre-fee* balance, allowing the account to dip into its reserve to pay the transaction fee without being rejected. This is the consistent XRPL policy across all reserve-sensitive transactors.

2. **Sequence anchoring**: The first ticket sequence is read directly from the account root's current `sfSequence` after the transaction machinery has already incremented it. A sanity check confirms this invariant: `txSeq == firstTicketSeq - 1` (or `txSeq == 0` for ticket-submitted transactions). This ensures the generated ticket sequences form a contiguous, non-overlapping block aligned to the account's sequence history.

3. **SLE creation loop**: For each ticket, a new `SLE` is created with `keylet::ticket(account_, curTicketSeq)`, populated with the account ID and ticket sequence, inserted into the ledger, and then wired into the account's owner directory via `dirInsert`. The `sfOwnerNode` field on each ticket records which directory page holds its entry, enabling efficient deletion later.

4. **Bulk sequence advance**: After the loop, `sfSequence` is set to `firstTicketSeq + ticketCount`. This is explicitly noted in the implementation comment as the only transaction in the XRPL that advances an account's sequence by more than one in a single application — all other transactors advance it by exactly one. This bulk advance is what makes the ticket sequences permanently reserved.

5. **Owner count and ticket count maintenance**: `adjustOwnerCount` is called with `+ticketCount` to update the account's reserve obligation, and `sfTicketCount` on the account root is incremented accordingly. Both fields must stay synchronized to make `preclaim`'s threshold check accurate in future transactions.

## Relationship to Sibling Transactors

Among the system-level transactors (`Batch.h`, `Change.h`, `LedgerStateFix.h`, `TicketCreate.h`), `TicketCreate` is the only one that directly allocates new ledger objects in bulk and requires the `Custom` consequences factory. The `Transactor` base also exposes `ticketDelete()` as a shared utility — it is used by `AccountDelete` to clean up outstanding tickets when an account is removed, maintaining the invariant that ticket SLEs cannot outlive their owning account root.