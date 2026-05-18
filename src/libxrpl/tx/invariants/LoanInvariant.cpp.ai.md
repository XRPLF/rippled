# `LoanInvariant.cpp` — ValidLoan Invariant Checker

## Role in the System

This file implements `ValidLoan`, one of several modular invariant-checker classes that plug into the XRPL `InvariantCheck` framework. Its purpose is to enforce a set of post-transaction consistency rules on `ltLOAN` ledger objects introduced by the XLS-66d Lending Protocol amendment. Invariant checkers serve as a last line of defense: they run after every transaction application — including failed ones — and can veto the commit entirely if ledger state has become incoherent. A `false` return from `finalize` causes the transaction to be rolled back and logged at `fatal` level, protecting the ledger from bugs or exploits that slip through higher-level validation.

## Two-Phase Visitor Pattern

Every invariant checker in the framework exposes exactly two methods. `visitEntry()` is called once per mutated SLE during transaction processing; `finalize()` is called after all entries have been visited to render the overall verdict. `ValidLoan` follows this contract precisely.

`visitEntry()` filters on `after->getType() == ltLOAN` and only then appends the `(before, after)` pair to `loans_`. The `before` snapshot may be null for newly created objects; `after` is always present because deleted entries are not of interest here — a deleted loan is simply absent from `loans_` and therefore skipped. Collecting pairs rather than performing checks inline is intentional: some invariants require the complete picture of a transaction's effects (e.g., cross-SLE relationships), though `ValidLoan` in its current form checks each loan independently.

## Invariants Enforced in `finalize()`

All four checks operate on the `after` SLE — the state that would be committed — using the `before` SLE only where a change comparison is needed. The checks are:

**Payment completion consistency (bidirectional).** If `sfPaymentRemaining == 0`, then `sfTotalValueOutstanding`, `sfPrincipalOutstanding`, and `sfManagementFeeOutstanding` must all be zero: a loan with no scheduled payments left must be fully settled. The reciprocal is equally enforced: if `sfPaymentRemaining != 0`, then all three outstanding fields must be non-zero — a loan cannot simultaneously have active payment obligations and a zeroed-out balance. These two checks are logically complementary and prevent any split state where the payment schedule and the outstanding balances diverge. The reference to the XLS-66d invariant specification (§3.2.2.3) is embedded in a comment, making it traceable to the protocol standard.

**Overpayment flag immutability.** The `lsfLoanOverpayment` flag is checked via `before->isFlag(...)` vs `after->isFlag(...)`, but only when `before` is non-null (i.e., for modifications, not creations). This prevents any transaction from toggling the overpayment flag mid-lifecycle; the flag is evidently meant to be set once and preserved.

**Non-negativity of fee and balance fields.** A range loop iterates over six `STNumber` fields — `sfLoanServiceFee`, `sfLatePaymentFee`, `sfClosePaymentFee`, `sfPrincipalOutstanding`, `sfTotalValueOutstanding`, `sfManagementFeeOutstanding` — verifying each is ≥ 0. Using `STNumber` (rather than `STAmount`) allows precise numeric comparisons against `beast::zero` and integer 0, which is appropriate for loan accounting fields that represent fixed-point decimals rather than XRP or IOU amounts.

**Strict positivity of `sfPeriodicPayment`.** A second range loop — structured identically but checking `<= 0` — enforces that the periodic payment amount is always strictly positive. A zero or negative payment amount would be economically nonsensical and could trigger divide-by-zero or underflow in payment schedule logic elsewhere.

## Relationship to the Broader Invariant Framework

`ValidLoan` is declared in `LoanInvariant.h` and aggregated into the master `InvariantCheck.h` alongside peers like `ValidLoanBroker`, `ValidVault`, and `AMMCheck`. The framework drives all checkers via a `std::tuple` visitor — every checker's `visitEntry` and `finalize` are called in sequence for each transaction, with a single logical `AND` over all results.

The companion `LoanBrokerInvariant.cpp` handles the `ltLOAN_BROKER` type and is substantially more complex: it traces directory page structure, validates that a broker's `sfCoverAvailable` is consistent with its pseudo-account's actual asset balance, checks vault references, and gated some checks behind the `fixSecurity3_1_3` amendment. `ValidLoan` by contrast is intentionally narrow — it validates each loan's internal numeric consistency without reference to the broker or vault objects that own it. This separation keeps each checker's scope well-bounded and independently testable.

The comment in `finalize()` — "Loans will not exist on ledger if the Lending Protocol amendment is not enabled, so there's no need to check it" — explains why there is no `view.rules().enabled(featureLendingProtocol)` guard. Because `ltLOAN` objects can only be created through amendment-gated transactions, the `loans_` vector will simply be empty on pre-amendment ledgers, making the entire loop a no-op. This is the cleanest possible amendment gating: the invariant itself is amendment-agnostic.