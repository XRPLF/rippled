# `TicketCreate.cpp` — Transactor for Batch Sequence-Number Reservation

## Role in the System

`TicketCreate` implements the XRP Ledger transaction that pre-reserves one or more sequence numbers by creating `ltTICKET` ledger objects. Tickets exist so accounts can submit transactions out of normal sequence order: a future transaction may reference a ticket rather than the account's current `sfSequence`, enabling parallel or advance-prepared signing workflows. This file contains the three-phase transactor logic — `preflight`, `preclaim`, and `doApply` — plus the custom `TxConsequences` factory that communicates the true sequence consumption to the transaction queue.

## The Three-Phase Transactor Pipeline

The XRPL transactor framework separates validation into distinct phases of increasing cost. `TicketCreate` overrides all three.

**`makeTxConsequences`** uses the `Custom` factory variant and passes `ctx.tx[sfTicketCount]` as the `sequencesConsumed` argument to `TxConsequences`. The default for a normal transaction is one consumed sequence, but a `TicketCreate` burns `ticketCount` sequence numbers in a single shot. Reporting this accurately to the `TxQ` prevents the queue from misjudging how much sequence space a pending transaction consumes, which would break ordering guarantees for subsequent transactions submitted by the same account.

**`preflight`** performs a stateless range check: `sfTicketCount` must be at least `minValidCount` (1) and at most `maxValidCount` (250). The 250 ceiling is deliberately empirical: the header's comment records that on a 2018 MacBook Pro in release mode, creating 250 tickets averaged 1.21 ms, matching the 1.25 ms average of a compute-intensive three-path Payment. The cap is therefore a CPU budget decision, not an arbitrary ledger policy.

**`preclaim`** enforces the per-account ticket ceiling before any ledger mutations occur. The logic is:

```cpp
curTicketCount + addedTickets - consumedTickets > maxTicketThreshold  →  tecDIR_FULL
```

The `consumedTickets` term equals 1 when the `TicketCreate` transaction itself is submitted via a ticket (the `SeqProxy` is a ticket rather than a sequence number). This one-ticket subtraction is not accidental: if an account at the 250-ticket limit submits a `TicketCreate` via a ticket to add one more, the net change is zero and the transaction should succeed. Without this correction the check would falsely reject it. The comment in the code notes that unsigned underflow cannot happen because `addedTickets >= 1` and `consumedTickets <= 1`.

## `doApply` — Ledger Mutations

**Reserve check.** The function first computes the reserve required after the new tickets are added:

```cpp
XRPAmount const reserve =
    view().fees().accountReserve(sleAccountRoot->getFieldU32(sfOwnerCount) + ticketCount);
if (preFeeBalance_ < reserve)
    return tecINSUFFICIENT_RESERVE;
```

`preFeeBalance_` (from the `Transactor` base class) holds the account balance *before* the transaction fee is deducted. Using the pre-fee balance is a conscious design choice documented in a comment: the account is allowed to consume its reserve in order to pay the fee, but the reserve XRP for newly created tickets must actually exist prior to fee payment. Checking against the post-fee balance would produce incorrect results in that edge case.

**Sequence anchoring.** Each ticket's `sfTicketSequence` field is set to a contiguous block starting at `firstTicketSeq = (*sleAccountRoot)[sfSequence]` — the account's sequence *after* the framework has already incremented it for this transaction. A sanity guard confirms this:

```cpp
if (std::uint32_t const txSeq = ctx_.tx[sfSequence];
    txSeq != 0 && txSeq != (firstTicketSeq - 1))
    return tefINTERNAL;
```

The `txSeq != 0` condition handles the case where the `TicketCreate` was itself submitted via a ticket (sequence field is 0 in that case). This guard is marked `LCOV_EXCL_LINE` because it is a defensive invariant the framework guarantees will never fire in production — it would only trip from a bug in the transaction machinery.

**Ticket creation loop.** For each ticket, the function allocates an `SLE` of type `ltTICKET`, sets `sfAccount` and `sfTicketSequence`, inserts it into the ledger, and registers it in the account's owner directory via `dirInsert`. The returned page number is stored in `sfOwnerNode` on the ticket SLE, enabling efficient deletion later without a full directory scan. The `tecDIR_FULL` path inside the loop is likewise excluded from coverage analysis since reaching it would require an abnormally enormous owner directory.

**Post-loop state updates.** After all tickets are created the function:

1. Increments `sfTicketCount` on the account root by `ticketCount`. This field mirrors the count of live tickets owned by the account and is what `preclaim` reads on future `TicketCreate` calls.
2. Calls `adjustOwnerCount(view(), sleAccountRoot, ticketCount, viewJ)` to raise `sfOwnerCount` by `ticketCount`, increasing the account's XRP reserve obligation.
3. Advances `sfSequence` to `firstTicketSeq + ticketCount`. The code explicitly notes (October 2018) that **`TicketCreate` is the only transaction in the XRPL protocol that can advance an account's `sfSequence` by more than one in a single transaction**. This is the mechanism by which the protocol "burns" those sequence numbers into reserved tickets rather than leaving gaps.

## Failure Modes and Error Codes

| Error | Phase | Condition |
|---|---|---|
| `temINVALID_COUNT` | preflight | `sfTicketCount` outside [1, 250] |
| `terNO_ACCOUNT` | preclaim | submitting account does not exist |
| `tecDIR_FULL` | preclaim | account would exceed 250 total tickets |
| `tecINSUFFICIENT_RESERVE` | doApply | pre-fee balance cannot cover the new reserve |
| `tecDIR_FULL` | doApply | owner directory insert fails (practically unreachable) |
| `tefINTERNAL` | doApply | account SLE missing or sequence invariant violated (both unreachable in practice) |

## Relationship to Other Components

`TicketCreate` depends on `keylet::ticket(account, seq)` from `Indexes.h` to derive the canonical ledger key for each ticket object, and on `keylet::ownerDir(account)` plus `dirInsert` from `DirectoryHelpers` to link tickets into the account's ownership chain. The `adjustOwnerCount` helper in `AccountRootHelpers` manages the `sfOwnerCount` field symmetrically with every other owned object type in the ledger. Ticket deletion mirrors this in reverse via `Transactor::ticketDelete`, a static helper exposed for use by `AccountDelete` when it cleans up an account's owned objects.