# `LoanSet.cpp` — Loan Creation Transactor

## Role in the System

`LoanSet.cpp` implements the `LoanSet` transaction, which is the entry point for creating a new loan in XRPL's on-ledger lending protocol (XLS-66). When applied, the transaction disburses principal from a vault's pool of assets to a borrower and records the full amortization schedule as a `Loan` ledger object. It sits at the intersection of the vault, loan broker, and borrower account subsystems, and must reconcile the financial properties of a structured loan with the protocol's ledger data model.

The lending protocol depends on the `SingleAssetVault` feature and its own feature flag, verified via `checkLendingProtocolDependencies()` in `checkExtraFeatures`.

## Dual-Signature Architecture

The most distinctive aspect of `LoanSet` is that it requires two parties to agree: the borrower (the transaction submitter, `sfAccount`) and the lender-side representative (the `sfCounterparty`, which defaults to the `LoanBroker`'s owner if omitted). The counterparty's consent is expressed through a `sfCounterpartySignature` sub-object embedded in the transaction. This sub-object can hold either a single signature (`sfTxnSignature`) or a multisignature list (`sfSigners`).

`calculateBaseFee` accounts for this by adding one extra `baseFee` per counterparty signer, mirroring the way the base transactor prices standard multisignatures. This prevents cheap DoS via bloated counterparty signature arrays. Notably, the base class's per-signer cost is *not* applied to the `CounterpartySignature` — `LoanSet` computes the count directly from the sub-object to avoid double-counting.

The batch inner transaction path is a deliberate carve-out: when `tfInnerBatchTxn` is set, `sfCounterparty` may be absent and `sfCounterpartySignature` is skipped entirely in `preflight`. This allows batch-orchestrated lending flows where co-signing is handled at a higher protocol layer.

## Validation Pipeline

`preflight` is purely stateless. It validates field formats and numeric ranges without touching the ledger:

- `sfPrincipalRequested` must be strictly positive.
- All rate fields (`sfInterestRate`, `sfLateInterestRate`, `sfCloseInterestRate`, `sfOverpaymentInterestRate`, `sfOverpaymentFee`) are checked against protocol-defined maximums using `validNumericRange`.
- Fee fields (`sfLoanServiceFee`, `sfLatePaymentFee`, `sfClosePaymentFee`) are checked for a non-negative minimum.
- `sfLoanOriginationFee` is validated against `sfPrincipalRequested` as an upper bound — it cannot exceed what is borrowed.
- `sfPaymentInterval` must be at least `minPaymentInterval` (60 seconds).
- `sfGracePeriod` is bounded above by the payment interval itself. A grace period longer than the interval would mean the late window for one payment overlaps the due date of the next, which the protocol prohibits.

`checkSign` resolves the counterparty identity (from `sfCounterparty` or from the broker's `sfOwner` field) and delegates to `Transactor::checkSign` once for the primary signer and again for the counterparty signature sub-object.

## Time Overflow Guard in `preclaim`

Before touching any ledger objects, `preclaim` performs a dedicated arithmetic overflow check. The last moment at which a payment can be due is `startDate + (paymentInterval × paymentTotal) + gracePeriod`. The `sfNextPaymentDueDate` field is `std::uint32_t`, so the protocol's time horizon is capped at `4,294,967,295` seconds (roughly year 2106 in POSIX time). If any combination of user-supplied intervals and totals would push the final grace period past that limit, the transaction returns `tecKILLED`.

This check is done using only the current ledger close time and the transaction fields, before acquiring any SLE locks, because it is cheap and catches a class of inputs that would otherwise require very large payment totals to detect.

## Business Rule Checks in `preclaim`

`preclaim` then loads the `LoanBroker`, `Vault`, and `Borrower` account objects and enforces:

- The broker must exist; if a `sfCounterparty` is specified explicitly but the broker is gone, `tecNO_ENTRY` is returned.
- Either `sfAccount` or `sfCounterparty` must be the broker's owner. This enforces that one party in every loan is the controlling lender.
- The vault must not have exceeded `sfAssetsMaximum` (if set).
- A two-stage precision check ensures every fee and principal value can be represented in the vault's asset type. For MPTs, which have integer-only precision, even a fractional fee would cause silent truncation; `tecPRECISION_LOSS` prevents this before any funds move.
- Frozen account checks cascade through all parties: the vault pseudo-account and borrower cannot be frozen (they are sending/receiving assets), the broker pseudo-account and broker owner cannot be *deep* frozen (they receive fee payments).

## `doApply` — Ledger Mutation

`doApply` materializes the loan. Its logic follows this sequence:

**1. Financial viability checks.** The vault's `sfAssetsAvailable` must cover `sfPrincipalRequested`. The broker's `sfDebtTotal + newDebtDelta` must not exceed `sfDebtMaximum`. The broker's first-loss capital (`sfCoverAvailable`) must satisfy the cover rate minimum against the new total debt. The cover check deliberately rounds the required cover *upward* via `NumberRoundModeGuard(Number::upward)`, making the solvency test conservative.

**2. Loan property computation.** `computeLoanProperties()` derives the full amortization structure: the periodic payment amount, the loan scale (the number of decimal places needed to represent periodic payments without rounding errors), and the initial `LoanState` — breaking down total outstanding value into principal, interest due, and management fee. A second precision check runs here against the computed loan scale; this is distinct from the `preclaim` check because the scale is not known until the amortization formula runs (relevant for IOU assets).

**3. Fund disbursement.** A single `accountSendMulti` call transfers two amounts simultaneously from the vault pseudo-account: `principalRequested - originationFee` to the borrower, and `originationFee` to the broker owner. The atomicity of this call means the origination fee can never be credited without also delivering the net principal. Trust lines or MPT holdings are created on-demand for the borrower and broker owner if they do not already exist.

**4. Loan SLE creation.** The `Loan` object is keyed by `keylet::loan(brokerID, loanSequence)`, giving each loan a stable, globally unique identity. A lambda `setLoanField` copies optional transaction fields onto the loan directly, correctly handling absent optional fields by applying their default values. The first `sfNextPaymentDueDate` is set to `startDate + paymentInterval`.

**5. Vault and broker state updates.** The vault's `sfAssetsAvailable` decrements by `principalRequested`, while `sfAssetsTotal` increments by the interest component (`state.interestDue`). This correctly reflects that the vault now holds a larger claim (principal + interest) but has less liquid cash available. The broker's `sfDebtTotal` is updated with `adjustImpreciseNumber`, which re-rounds to the vault scale and clamps to zero to absorb accumulated floating-point dust. The broker's `sfLoanSequence` is incremented; wrapping to zero returns `tecMAX_SEQUENCE_REACHED`.

**6. Directory linking.** The loan is linked into both the broker pseudo-account's directory (`sfLoanBrokerNode`) and the borrower's owner directory (`sfOwnerNode`). The borrower's owner count is incremented first and checked against their XRP reserve *before* funds move, ensuring the borrower can always maintain the reserve obligation that the new loan object creates.

## Relationship to Sibling Files

`LoanPay.cpp` handles subsequent payments against loans created here. `LoanDelete.cpp` handles cleanup when a loan reaches zero balance. `LoanBrokerSet.cpp` establishes the broker object that `LoanSet` requires. `LendingHelpers.cpp` provides the shared amortization mathematics (`computeLoanProperties`, `constructLoanState`, `checkLoanGuards`) that `LoanSet` delegates all financial calculation to, keeping the transactor itself focused on ledger mutation rather than financial modeling.