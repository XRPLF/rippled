# `LoanDelete.h` — Loan Deletion Transactor

## Role in the System

`LoanDelete` is the transactor responsible for removing a fully-repaid loan object from the XRPL ledger. It lives within the on-chain lending protocol (`xls-66`) alongside `LoanSet`, `LoanPay`, `LoanManage`, and the loan broker family of transactors. While `LoanSet` establishes a loan and `LoanPay` services it, `LoanDelete` closes out the loan's lifecycle once the borrower has satisfied all payment obligations, freeing the owner-count reservations that were held against both the borrower's account and the loan broker's pseudo-account.

## Class Structure and the Transactor Pipeline

`LoanDelete` publicly inherits from `Transactor` and follows the standard three-phase compile-time polymorphic dispatch that the framework mandates. The class exposes no constructor beyond the inline delegating one that simply forwards `ApplyContext` to the base. The `ConsequencesFactory` is set to `Normal`, indicating that failing this transaction does not block subsequent transactions from the same account in a batch — appropriate because a loan-deletion failure leaves the ledger state unchanged and has no impact on sequence numbers or fee eligibility for other operations.

The four key methods — `checkExtraFeatures`, `preflight`, `preclaim`, and `doApply` — are static for the first three and virtual for the last, matching the pattern enforced by `Transactor::invokePreflight<T>`. This is not polymorphism through vtables but name-hiding: the framework template instantiates the correct static overrides at compile time, so each phase runs in isolation from the others.

## Validation Phases

`checkExtraFeatures` delegates entirely to `checkLendingProtocolDependencies(ctx)`, defined in `LendingHelpers.h`. This call verifies that all required amendments enabling the lending protocol are active in the current rules set. By separating this check from `preflight`, the framework can gate the transaction at the feature-flag level before any field-level validation even begins.

`preflight` is deliberately minimal: it only verifies that `sfLoanID` is not the zero hash. All other checks — existence, ownership, and state — would require ledger access and are therefore deferred to `preclaim`. This respects the phase contract: `preflight` runs without a ledger view and must stay cheap and stateless.

`preclaim` performs the substantive pre-execution guards. It reads the loan object via `keylet::loan(loanID)` and enforces two invariants before allowing the transaction to proceed. First, the loan's `sfPaymentRemaining` field must be zero — an active loan with outstanding installments cannot be deleted, returning `tecHAS_OBLIGATIONS`. This is the core business rule: loan cleanup is only permitted once the borrower has fully amortized the debt. Second, the caller must be either the broker owner (identified by traversing the `LoanBroker` SLE from the loan's `sfLoanBrokerID`) or the direct borrower (`sfBorrower`). Any other account gets `tecNO_PERMISSION`. If the `LoanBroker` SLE is somehow absent despite the loan referencing it, the method returns `tecINTERNAL` and marks that branch `LCOV_EXCL_LINE` — a defensive impossible-path guard.

## Application Logic

`doApply` performs the multi-step ledger mutation. It resolves the loan, borrower account, loan broker, and vault objects via `view.peek()`. Each missing SLE returns `tefBAD_LEDGER` (all marked `LCOV_EXCL_LINE` since `preclaim` guaranteed these objects exist). The cleanup sequence is:

1. **Directory removal**: The loan's key is evicted from the `ownerDir` of the broker's pseudo-account (using `sfLoanBrokerNode` as the directory node hint) and from the borrower's `ownerDir` (using `sfOwnerNode`).
2. **SLE erasure**: The loan ledger object is deleted with `view.erase(loanSle)`.
3. **Broker owner-count decrement**: `adjustOwnerCount` reduces the broker's `sfOwnerCount` by one. The broker's owner count specifically tracks outstanding loans, distinct from the pseudo-account's own count.
4. **Dust debt forgiveness**: If the decremented owner count reaches zero — meaning this was the last loan under the broker — the broker's `sfDebtTotal` is checked. Any residual non-zero amount is forcibly zeroed. The surrounding `XRPL_ASSERT_PARTS` call verifies that the remaining debt rounds to zero given the vault's asset scale, documenting the invariant that only rounding dust can remain when all loans are repaid. This prevents an accumulation of sub-asset-precision amounts from stranding the broker in an uncleanable state.
5. **Borrower owner-count decrement**: `adjustOwnerCount` also decreases the borrower's count, releasing the XRP reserve that was locked when the loan was created.
6. **Asset association**: `associateAsset` is called on the loan, broker, and vault SLEs as a safety measure. The comment notes these calls "shouldn't do anything" at this point but are included defensively to ensure any asset-tracking side effects are consistent.

## Design Observations

The permission model allows either party to initiate deletion. This is intentional: both the broker (seeking to reclaim administrative state) and the borrower (seeking to recover their owner reserve) have valid incentives to clean up after full repayment. Neither can do so prematurely because the `sfPaymentRemaining` guard in `preclaim` is absolute.

The dust-forgiveness path for the final loan is a pragmatic safeguard against cumulative fixed-point rounding errors. The lending protocol uses `Number` arithmetic with explicit rounding modes, but over many payment periods, sub-precision residuals in `sfDebtTotal` can accumulate. Rather than leaving the broker's debt balance permanently non-zero after all loans are closed, the transactor zeroes it with an assertion that bounds the magnitude of the forgiven amount. This keeps the lending protocol's invariants clean without burdening individual payment transactions with perfect precision requirements.