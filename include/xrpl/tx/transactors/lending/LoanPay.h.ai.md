# `LoanPay.h` — Loan Repayment Transactor

## Role in the System

`LoanPay.h` declares the `LoanPay` transactor, the on-ledger entry point for borrowers repaying obligations created by the XRPL Lending Protocol (XLS-66). It fits into the standard XRPL transaction pipeline as a subclass of `Transactor`, adding lending-specific validation and multi-destination fund routing on top of the common sequencing, fee, and signature machinery.

The header is deliberately thin — a forward declaration of three static lifecycle hooks (`checkExtraFeatures`, `preflight`, `preclaim`), a custom `calculateBaseFee`, and the virtual `doApply`. All implementation lives in `LoanPay.cpp`, which pulls in `LendingHelpers.h` for payment computation and `LoanManage.h` for loan impairment utilities.

---

## Transaction Lifecycle

### `checkExtraFeatures` — Amendment Gating

Rather than scattering amendment checks throughout `preflight`, `LoanPay` delegates to `checkLendingProtocolDependencies`, a shared helper that verifies all feature flags the lending protocol depends on are active. If any dependency is absent, `invokePreflight` returns `temDISABLED` before any field parsing occurs.

### `getFlagsMask` — Mutually Exclusive Payment Modes

`LoanPay` declares `tfLoanPayMask` as its flags mask. Three behavioural modifiers are valid: `tfLoanLatePayment`, `tfLoanFullPayment`, and `tfLoanOverpayment`. A key design invariant — enforced with `std::popcount` in `preflight` — is that these flags are **mutually exclusive**. A borrower may choose exactly one payment mode per transaction. The static assert confirms that these three bits together cover exactly the bits not in `tfLoanPayMask | tfUniversal`, providing a compile-time guarantee that the mask and the flags stay in sync. Regular scheduled payments use no flag at all.

### `preflight` — Structural Validity

Two field checks run before signature verification: the `sfLoanID` field must be non-zero, and `sfAmount` must be positive. Flag mutual-exclusivity is confirmed here. These are pure structural checks; no ledger state is consulted.

---

## Custom Fee Calculation

`calculateBaseFee` is the most algorithmically interesting method in the header. The normal fee covers a single payment; but when a borrower submits a large amount — implying multiple installments will be processed in one transaction — the ledger charges proportionally more.

The logic reads the loan's `sfPaymentRemaining`, checks whether the payment is late (which caps it at one installment's work), then estimates `numPaymentEstimate = amount / regularPayment`. Fees are charged at one base unit per `loanPaymentsPerFeeIncrement` payments, rounded up. The rationale is computational fairness: processing ten amortization steps costs the network roughly ten times the work. If the loan or vault objects cannot be found, the method falls back to the normal fee and lets `preclaim` produce the authoritative error.

---

## `preclaim` — Ledger State Validation

`preclaim` loads the `Loan`, `LoanBroker`, and `Vault` objects from the read-only ledger view and enforces:

- **Ownership**: Only the loan's `sfBorrower` may submit this transaction (`tecNO_PERMISSION`).
- **Overpayment permission**: If `tfLoanOverpayment` is set but the loan's `lsfLoanOverpayment` flag is absent, the transaction fails. The error code is `tecNO_PERMISSION` when `fixSecurity3_1_3` is enabled, or the legacy `temINVALID_FLAG` otherwise — a versioned correction to historical behaviour.
- **Loan completeness**: If `sfPaymentRemaining == 0` or `sfPrincipalOutstanding == 0`, the loan is already fully discharged (`tecKILLED`).
- **Asset consistency**: The transaction's `sfAmount` asset must match the vault's `sfAsset`.
- **Freeze and authorization**: Both the borrower account and the vault's pseudo-account are checked for frozen and deep-frozen states; the borrower must also hold appropriate authorization to transact the asset.
- **Balance sufficiency**: The borrower must hold at least the full submitted amount, even if the actual payment applied consumes less. Partial payment semantics are explicitly rejected — if the transaction specifies amount X, the account must have X available.

The `LCOV_EXCL_*` markers on the "vault does not exist" and "broker does not exist" paths confirm that referential integrity between Loan → LoanBroker → Vault is maintained by the protocol and treated as invariant.

---

## `doApply` — Three-Object State Machine

`doApply` coordinates simultaneous mutations across three mutable ledger objects (Loan, LoanBroker, Vault) and one or two fund movements.

**Impairment unwind**: If the loan carries `lsfLoanImpaired`, `LoanManage::unimpairLoan` restores the loan's tracked fields to their pre-impairment state before any payment arithmetic runs. If unimpairing fails, the transaction aborts and the sandbox discards all changes.

**Payment type dispatch**: The transaction flags determine which `LoanPaymentType` enum value (`regular`, `late`, `full`, `overpayment`) is passed to `loanMakePayment`. This function (defined in `LendingHelpers`) executes the amortization math and modifies the `loanSle` fields in place, returning a `LoanPaymentParts` structure describing how the payment breaks down into `principalPaid`, `interestPaid`, `feePaid`, and `valueChange`.

**Broker fee routing**: Before moving funds, `doApply` decides whether the broker's service fee goes to the **broker owner** or to the **broker's pseudo-account** (the first-loss cover pool). The decision weighs:
1. Whether cover available ≥ minimum required (computed conservatively using upward rounding against `coverRateMinimum * debtTotal`).
2. Whether the broker owner is deep-frozen for the asset.
3. Whether the broker owner holds the required authorization.

If all three conditions are satisfied, the fee flows to the owner's personal account; otherwise it accumulates in the broker pseudo-account as cover capital. This prevents a single frozen/unauthorized state from blocking an otherwise valid repayment.

**Vault accounting**: `assetsAvailable` increases by `totalPaidToVaultRounded` (principal + interest, rounded down to vault scale to avoid crediting fractions smaller than the vault can represent). `assetsTotal` adjusts by `paymentParts.valueChange`, which is non-zero only for late, full, and overpayment modes. The broker's `sfDebtTotal` is reduced by `totalPaidToVaultForDebt = totalPaidToVaultRaw - valueChange`, ensuring that value changes that alter the loan's outstanding balance are correctly reflected in the broker's aggregate exposure. `adjustImpreciseNumber` re-rounds this field to vault scale and floors it at zero to absorb accumulated rounding drift when a broker carries loans at differing scales.

**Fund movement**: A single `accountSendMulti` call transfers funds from the borrower to two destinations — the vault pseudo-account and the broker payee — atomically and without transfer fees (`WaiveTransferFee::Yes`). Transfer fee waiver is intentional: the lending protocol operates on asset amounts computed by the amortization schedule, and imposing an extra transfer fee would distort those calculations.

**Debug conservation checks**: In `NDEBUG`-disabled builds, a series of `XRPL_ASSERT_PARTS` calls verify that the sum of borrower, vault, and broker balances is identical before and after the transfer, that no balance goes negative, and that the vault's `sfAssetsAvailable` field exactly matches the actual token balance held by the vault pseudo-account. These assertions catch rounding or accounting bugs without affecting production performance.

---

## Design Notes

`ConsequencesFactory{Normal}` means a failed `LoanPay` does not block subsequent transactions in the same account queue — the transactor does not guarantee state changes that would make later transactions impossible. The custom fee calculation is a deliberate departure from the base class's flat fee, motivated by ensuring that users who batch many installments into one large payment are not subsidised by the rest of the network. The strict no-partial-payment rule in `preclaim` simplifies the invariant that `doApply` can assume: the submitted amount is always fully available, so no fallback logic is needed to handle a borrower who runs out of funds mid-computation.