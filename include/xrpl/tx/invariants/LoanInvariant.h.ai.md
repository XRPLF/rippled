# `LoanInvariant.h` — ValidLoan Invariant Checker

## Role in the System

This header is part of the XRPL invariant-checking framework and defines `ValidLoan`, the post-transaction sanity checker for `ltLOAN` ledger entries introduced by the XLS-66 Lending Protocol amendment. The XRPL invariant system acts as a last line of defense: after every transaction is applied, a suite of checkers runs to ensure that ledger state has not drifted into an impossible or internally inconsistent configuration. `ValidLoan` is one entry in the `InvariantChecks` tuple declared in `InvariantCheck.h`, sitting alongside other domain-specific checkers like `ValidLoanBroker`, `ValidVault`, and `ValidAMM`.

## The Two-Phase Checker Pattern

All invariant checkers in this codebase share the same structural contract — two public methods that the framework calls in sequence:

**`visitEntry(bool isDelete, before, after)`** is called once for every ledger object touched during transaction processing. `ValidLoan` uses this phase purely for data collection: if the post-transaction `SLE` exists and its type is `ltLOAN`, the `(before, after)` pair is pushed into `loans_`. The `before` pointer can be null (for newly created objects), and the `after` pointer can be null for deletions — the implementation checks both before dereferencing.

**`finalize(tx, tec, fee, view, journal)`** is called once after all entries have been visited. This is where all actual validation logic runs. The method iterates over the collected `loans_` pairs and enforces each invariant in turn, returning `false` and logging a `fatal`-level journal message on any violation.

The decision to collect entries in `visitEntry` and reason about them in `finalize` follows the pattern used consistently across every checker in this module. It separates data-gathering (which must be cheap and stateless per-entry) from validation (which may need to reason across multiple related entries or read the full ledger view).

## Invariants Enforced

The implementation checks four categories of constraint, all grounded in the XLS-66 spec (referenced explicitly in the source via a link to the XRPL Standards repo):

**Payment-completion consistency**: If `sfPaymentRemaining` is zero, the loan must be fully settled — meaning `sfTotalValueOutstanding`, `sfPrincipalOutstanding`, and `sfManagementFeeOutstanding` must all be zero. The inverse is also checked: if any of those outstanding amounts are zero but `sfPaymentRemaining` is non-zero, that is equally invalid. These two symmetrical checks together enforce a strict bijection between the payment schedule being exhausted and the economic balance being cleared.

**Immutability of the overpayment flag**: The `lsfLoanOverpayment` flag, once set or cleared, must not change during a transaction. This guards against any processing path that might inadvertently flip this flag as a side-effect.

**Non-negative fee and balance fields**: Six `STNumber` fields — `sfLoanServiceFee`, `sfLatePaymentFee`, `sfClosePaymentFee`, `sfPrincipalOutstanding`, `sfTotalValueOutstanding`, and `sfManagementFeeOutstanding` — are checked to ensure they cannot fall below zero. Since `STNumber` can represent signed arbitrary-precision values, this guard is non-trivial.

**Strictly positive periodic payment**: `sfPeriodicPayment` must be greater than zero. A zero or negative periodic payment would represent a structurally malformed loan that could produce undefined amortization behavior.

## Design Observations

The `loans_` member stores `std::pair<SLE::const_pointer, SLE::const_pointer>` — shared pointers to the pre- and post-state of each loan. The `before` pointer is used only for the overpayment flag comparison (since that check is explicitly about change detection rather than absolute state). All other checks operate on `after`, meaning they validate the ledger state as it would be committed, not the intermediate diff.

The `finalize` method ignores the transaction type (`STTx`), the fee (`XRPAmount`), and the `ReadView` entirely — a signal that loan consistency can be verified locally from the collected objects alone, without requiring lookup into the broader ledger. This is intentional: any information needed from the ledger should be collected during `visitEntry` when the framework walks the modified set.

The comment at the top of `finalize` — "Loans will not exist on ledger if the Lending Protocol amendment is not enabled" — documents why there is no amendment guard around the logic itself. If the amendment is off, `visitEntry` will never collect any `ltLOAN` entries, so the loop is trivially empty and `finalize` returns `true` immediately. The checker is safe to include unconditionally in `InvariantChecks`.