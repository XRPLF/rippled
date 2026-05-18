# `LendingHelpers.h` — Lending Protocol Computation Primitives

This header is the computational core of the XLS-66 lending protocol. Every transactor in the `lending/` directory — `LoanSet`, `LoanPay`, `LoanDelete`, and the `LoanBroker*` family — either calls these functions directly or depends on structures defined here. The file does not model network behavior or ledger I/O; instead it defines the mathematics of amortized loan payments, the data types that carry that mathematics through the protocol, and a small set of utility functions that translate between ledger objects and those types.

## Protocol Gating

`checkLendingProtocolDependencies()` is the single function that gates the entire lending protocol. It checks that the `featureSingleAssetVault` amendment is active and that vault creation is permitted in the current context. Every lending transactor's `checkExtraFeatures()` calls this function, so adding a new amendment requirement here affects all transactions atomically.

## The Data Type Hierarchy

Four structures form a layered view of a loan's financial state, each at a different level of abstraction:

**`LoanState`** is the most fundamental: it holds the four numbers that describe where a loan stands — `valueOutstanding`, `principalOutstanding`, `interestDue`, and `managementFeeDue`. The key invariant is that `interestDue + managementFeeDue == valueOutstanding - principalOutstanding`; this is enforced at runtime by `XRPL_ASSERT_PARTS` inside `interestOutstanding()`. The `managementFeeDue` field tracks the broker's share of the interest, kept separate so the two recipients (vault and broker) can be paid the correct portions.

**`LoanProperties`** bundles everything needed to describe a fully initialized loan or a re-amortized one: the `periodicPayment` (unrounded), the current rounded `LoanState`, the computed `loanScale`, and `firstPaymentPrincipal`. The `loanScale` is not fixed at loan creation — it is derived dynamically from the `STAmount` exponent of the total value outstanding, then clamped to a minimum. This ensures that all subsequent rounding of principal, interest, and fees uses a consistent number of decimal places, preventing a class of dust-accumulation bugs where tiny remainders can never be cleared.

**`LoanPaymentParts`** is the output of a completed payment: how much went to principal (`principalPaid`), how much to the vault as interest (`interestPaid`), how much to the broker as fees (`feePaid`), and a `valueChange` that records whether the loan's total outstanding moved in an unexpected direction. For a well-timed regular payment, `valueChange` is zero. For a late payment, it is positive (the penalty interest increased the debt). For an overpayment, it is negative (extra principal payment reduced future interest). The `operator+=` is provided so that multiple consecutive payment rounds can be accumulated into a single result.

**`detail::LoanStateDeltas`** is the difference between two `LoanState` objects, computed by `operator-(LoanState, LoanState)`. This allows `tryOverpayment()` to measure accumulated rounding error — the gap between what the loan theoretically should owe and what the ledger's rounded values actually record — and carry that error forward into the re-amortized state.

## Tracked vs. Untracked Amounts

A critical architectural distinction separates `detail::PaymentComponents` from `detail::ExtendedPaymentComponents`. Tracked amounts (`trackedValueDelta`, `trackedPrincipalDelta`, `trackedManagementFeeDelta`) reduce the Loan ledger object's stored fields (`sfTotalValueOutstanding`, `sfPrincipalOutstanding`, `sfManagementFeeOutstanding`). These are the numbers that appear in the amortization schedule and drive future payment calculations. Untracked amounts (`untrackedManagementFee`, `untrackedInterest`) are paid out to the broker and vault respectively but do not alter the amortization schedule — they represent charges that have no scheduled counterpart, such as late payment fees, overdue penalty interest, or service fees. `ExtendedPaymentComponents` is constructed from a `PaymentComponents` base plus these two extra numbers, with `totalDue` computed inline in its constructor as `trackedValueDelta + untrackedInterest + untrackedManagementFee`.

## Rounding Policy

The spec requires that borrowers never benefit from rounding: periodic payments are always rounded upward via `roundPeriodicPayment()`, which delegates to `roundToAsset(..., Number::upward)`. The `isRounded()` helper checks whether a value is already at the target precision by comparing the upward and downward rounded forms — if they match, no further precision exists.

The template `adjustImpreciseNumber()` handles a subtler problem. Certain loan values are re-rounded to vault scale every time they are adjusted, preventing the accumulation of rounding dust across many payments. It additionally clamps the result to zero if it would go negative — a defensive guard against off-by-one rounding errors that could otherwise leave tiny negative balances.

## Core Computation Functions

`loanPeriodicRate()` converts an annualized rate (expressed in tenth-bips) to a per-period rate by prorating it against `secondsInYear` (the `constexpr` constant for 365 days in seconds). All amortization math flows from this single conversion.

`computeLoanProperties()` (with two overloads, one taking a raw `TenthBips32` rate and one a pre-converted `periodicRate`) encapsulates the XLS-66 Section A-2 equations for computing the initial loan state. It calculates the periodic payment, derives the `loanScale` from the total value's `STAmount` exponent, rounds all components consistently, and populates `firstPaymentPrincipal` — the unrounded principal share of the very first payment. This last field is checked as a guard condition in `checkLoanGuards()`: if it would round to zero, no payment can ever reduce the principal, and the loan must be rejected.

`computeTheoreticalLoanState()` produces what the ledger values *should* be at a given point in the schedule, without any rounding. `constructLoanState()` and `constructRoundedLoanState()` produce `LoanState` values from arbitrary inputs or directly from the SLE fields, respectively.

## The Overpayment Path

`detail::tryOverpayment()` is the most complex function in the file. When a borrower pays more principal than scheduled, the remaining payments must be re-amortized from the new lower principal. The function cannot simply recalculate from scratch — it must preserve the accumulated rounding errors from the loan's history to maintain consistency. It does this by computing the theoretical (unrounded) state, measuring the error gap against the current rounded ledger state, and then adding that same error back to the newly re-amortized theoretical state before rounding again. The entire calculation runs against local copies of the loan state (a "sandbox"), and only if the result passes all guard conditions — the principal decreased, the new periodic payment is positive, and `checkLoanGuards()` succeeds — are the ledger proxy objects updated. If the overpayment would leave the loan in an invalid state, the function returns `Unexpected(tesSUCCESS)`, which causes the overpayment to be silently ignored rather than failing the entire transaction.

## Entry Point

`loanMakePayment()` is the public entry point that `LoanPay::doApply()` calls. It accepts a `LoanPaymentType` enum value (`regular`, `late`, `full`, or `overpayment`) and dispatches to the appropriate internal calculation path, returning `Expected<LoanPaymentParts, TER>`. All the structures and helper functions in this file exist to make that single entry point correct across every payment scenario the spec defines.