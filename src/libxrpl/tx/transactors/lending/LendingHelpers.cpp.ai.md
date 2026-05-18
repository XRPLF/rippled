# `LendingHelpers.cpp` — XRPL Lending Protocol Amortization Engine

This file is the numerical core of the XRPL lending protocol (XLS-66 specification). It implements every mathematical operation involved in a loan's life cycle: computing amortized periodic payments, splitting each payment into principal, interest, and management-fee components, handling late payments, early-closure ("full") payments, and overpayments that trigger re-amortization. The top-level entry point `loanMakePayment()` ties these together into the `make_payment` function defined in XLS-66 §3.2.4.4.

## Amortization Foundations

The file begins with a group of low-level formulas that map directly to numbered equations in the XLS-66 Equation Glossary (Section A-2):

- `loanPeriodicRate()` (Eq. 1) converts an annualized rate in tenth-of-a-basis-point units (`TenthBips32`) to a per-payment-interval rate by prorating against `secondsInYear`.
- `computeRaisedRate()` (Eq. 5) computes `(1 + r)^n` and `computePaymentFactor()` (Eq. 6) derives the standard amortization factor `r(1+r)^n / ((1+r)^n - 1)`, handling the zero-interest special case cleanly.
- `loanPeriodicPayment()` (Eq. 7) applies the factor to produce the fixed installment amount; `loanPrincipalFromPeriodicPayment()` (Eq. 10) is the inverse, recovering what the principal should be given a periodic payment.

The zero-interest path is handled explicitly throughout: when `periodicRate == 0`, equal principal slices replace the exponential formula, avoiding division-by-zero while keeping the same code path.

## State Representation and the Theoretical/Rounded Split

Two key design concerns permeate the entire file: _what the loan state should be_ at full mathematical precision (the **theoretical** state), and _what it actually is_ after rounding to the asset's representable scale (the **rounded/ledger** state).

`LoanState` holds four fields — `valueOutstanding`, `principalOutstanding`, `interestDue`, `managementFeeDue` — with `interestDue` always derived from the others to guarantee consistency. `LoanProperties` bundles these with the periodic payment, the loan's rounding scale, and the first-payment principal (a canary value used to detect precision loss).

`computeTheoreticalLoanState()` (XLS-66 §3.2.4.4, Eqs. 30–33) computes the loan state purely from the amortization schedule at full precision. `constructLoanState()` and `constructRoundedLoanState()` build `LoanState` from actual ledger field values. The difference between these two representations is the accumulated rounding error — a small but important number that must be carried forward whenever the loan is re-amortized (e.g., after an overpayment).

## Payment Components: Tracked vs. Untracked

`detail::PaymentComponents` represents the ledger-visible deltas: what will be subtracted from `sfTotalValueOutstanding`, `sfPrincipalOutstanding`, and `sfManagementFeeOutstanding` in the Loan ledger object. `detail::ExtendedPaymentComponents` extends this with two untracked amounts — `untrackedInterest` (late penalty interest paid directly to the vault) and `untrackedManagementFee` (service fees, late fees paid directly to the broker). The `totalDue` field is computed in the constructor as the sum, ensuring the borrower's check amount is always against a single authoritative figure.

The outer `LoanPaymentParts` struct is what gets returned all the way up to `LoanPay`, summarising what the caller should actually move between accounts: `principalPaid` (to vault), `interestPaid` (to vault), `feePaid` (to broker), and `valueChange` (the sign indicating whether the loan's tracked value increased or decreased beyond normal schedule).

## Computing Regular Payment Components

`computePaymentComponents()` is responsible for splitting a single scheduled installment. Its algorithm avoids recomputing the formula from scratch; instead it asks "what should the loan state be after this payment?" by calling `computeTheoreticalLoanState(paymentRemaining - 1)` and taking the delta between the current ledger state and that target. This naturally absorbs accumulated rounding errors without explicit error-tracking. After computing deltas the function applies a series of caps (`std::min`) and the `addressExcess` lambda to ensure no component exceeds the available balance or the periodic payment. When `paymentRemaining == 1` or the total outstanding ≤ the payment, a final-payment path zeroes every tracked field, guaranteeing clean loan closure.

## The Template Proxy Pattern

`doPayment()` is templated on `NumberProxy`, `UInt32Proxy`, and `UInt32OptionalProxy`. This allows the same function to run against `ValueProxy<Number>` and `ValueProxy<uint32_t>` objects (which write through to the actual Loan SLE) and against plain `Number`/`uint32_t` values used in simulation. The `XRPL_ASSERT_PARTS` calls then fire in both contexts, giving consistent validation whether running from the real transaction engine or a unit test.

## Late Payments

`computeLatePayment()` first verifies the due date has passed via `hasExpired()`; if not, it returns `tecTOO_SOON`. The penalty interest is computed by `loanLatePaymentInterest()` (Eq. 16), which calculates how many seconds the payment is overdue and calls `loanPeriodicRate()` with that interval. The penalty is then split into vault interest and broker fee using `computeInterestAndFeeParts()`, both parts added as untracked amounts on top of the regular periodic payment. The borrower's provided `amount` must cover `late.totalDue` or the transaction fails with `tecINSUFFICIENT_PAYMENT`.

## Full (Early-Closure) Payments

`computeFullPayment()` handles voluntarily paying off the loan before the final scheduled installment. It is explicitly disallowed when only one payment remains (which should follow the normal path). The function reverse-calculates the theoretical principal from `loanPrincipalFromPeriodicPayment()`, then computes two cost components via `computeFullPaymentInterest()`: accrued interest since the last payment (Eq. 27) and a prepayment penalty (Eq. 28). The `untrackedInterest` field in the resulting `ExtendedPaymentComponents` is set to `roundedFullInterest - totalInterestOutstanding`; this can be negative (early payoff saves more interest than the penalty costs) or positive (penalty exceeds the discounted interest), and the sign drives the `valueChange` field in the returned `LoanPaymentParts`.

## Overpayments and Re-Amortization

Overpayments are the most algorithmically complex case. When a borrower pays more than the periodic amount, the surplus reduces principal immediately, but because the remaining payment schedule was computed assuming a higher principal, the periodic payment must be recalculated — a process called re-amortization.

`tryOverpayment()` handles this in a sandbox: it captures the accumulated rounding error (`roundedOldState - theoreticalState`), reduces the theoretical principal by the overpayment, calls `computeLoanProperties()` for the new schedule, adds the rounding errors back (`newTheoreticalState + errors`), then rounds all three components back to `loanScale` using conservative clamping. It validates the result with `checkLoanGuards()` and rejects if any invariant is violated — returning `Unexpected(tesSUCCESS)` (a deliberate non-error signal meaning "ignore the overpayment silently"). The principal must strictly decrease; if it doesn't, the overpayment is rejected.

`doOverpayment()` wraps `tryOverpayment()` and, only after all validations pass, writes the new values through the proxy objects to the actual ledger.

## Loan Guards and Precision Loss Detection

`checkLoanGuards()` enforces four invariants at loan-creation and re-amortization time:

1. If an interest rate is set, total interest must be a measurable positive number — otherwise the amortization table is meaningless.
2. The first-payment principal (pre-computed in `computeLoanProperties()`) must be positive at full precision. This prevents loans where the principal component rounds to zero every period, meaning principal would never actually be paid down.
3. The rounded periodic payment must not be zero.
4. The total value divided by the rounded payment (with upward rounding) must equal the scheduled payment count, ensuring the loan will actually complete in the specified number of installments.

All four failures return `tecPRECISION_LOSS`, surfacing the precision constraint as a transaction error rather than a silent arithmetic drift.

## `loanMakePayment()` Dispatch

The top-level function reads all relevant loan fields via `ValueProxy` objects — which lazily back-propagate writes to the SLE — and branches on `LoanPaymentType` (regular, late, full, overpayment). The regular and overpayment paths share a `while` loop that processes up to `loanMaximumPaymentsPerTransaction` installments, accumulating results in `LoanPaymentParts`. The overpayment phase appends only if the remaining `amount - totalPaid` is positive, the loan flag `lsfLoanOverpayment` is set, and `trackedPrincipalDelta > 0` after fee deduction. Final `XRPL_ASSERT` calls at the exit verify that all returned amounts are rounded and non-negative, providing a last-resort consistency check across all code paths.