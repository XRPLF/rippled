# `LoanSet.h` — Loan Creation Transactor for the XRPL Lending Protocol

## Role and Context

`LoanSet` is the transactor that creates a new loan object on the XRP Ledger, implementing the XLS-66 lending protocol. It sits in the `include/xrpl/tx/transactors/lending/` module alongside `LoanPay`, `LoanDelete`, `LoanManage`, and the `LoanBroker*` family of transactors. Where `LoanPay` services an existing loan and `LoanDelete` terminates one, `LoanSet` is the origination step: it validates loan terms, computes the full amortization schedule, transfers principal from a vault to a borrower, deducts any origination fee, and creates the persistent `Loan` ledger entry that all subsequent transactions operate against.

The header is deliberately thin — it declares the class interface and its protocol constants, while the full logic lives in `LoanSet.cpp`.

## Inheritance and the Static-Polymorphism Transactor Pattern

`LoanSet` inherits from `Transactor` and uses the same static compile-time polymorphism that the entire transactor framework employs. The base class `invokePreflight<T>` template calls `T::checkExtraFeatures`, `T::getFlagsMask`, `T::preflight`, and `T::preflightSigValidated` by name — no vtable, just name hiding. `LoanSet` overrides each of the meaningful hooks while the base class provides safe no-op defaults for the rest. `ConsequencesFactory` is set to `Normal`, meaning the transaction does not unconditionally block later transactions from an account (as opposed to the `Blocker` variant used by escrow or account deletions).

## Transaction Lifecycle

**`checkExtraFeatures`** delegates directly to `checkLendingProtocolDependencies`, which verifies that all XLS-66 prerequisite amendments are active. This is the canonical way to gate an entire sub-protocol behind a feature flag without scattering amendment checks throughout the preflight body.

**`getFlagsMask`** returns `tfLoanSetMask`, restricting which transaction flags are valid. Only the `tfLoanOverpayment` flag is meaningful here (it controls whether the borrower is permitted to pay ahead of schedule); any other flag bits are rejected in `preflight0`.

**`preflight`** performs the stateless validation pass. It enforces a critical design requirement of the lending protocol: every `LoanSet` must carry a second cryptographic signature from the counterparty (the broker's owner), because a loan binds both the lender's vault and the borrower. The one exception is when the transaction travels inside a `Batch` inner transaction (`tfInnerBatchTxn`), where batch-level authorization replaces the counterparty signature. Rate fields (`sfInterestRate`, `sfLateInterestRate`, `sfCloseInterestRate`, etc.) are range-checked against protocol maxima. The `sfGracePeriod` must fall between `defaultGracePeriod` (60 seconds) and the `paymentInterval`, and `paymentInterval` itself must be at least `minPaymentInterval` (60 seconds) — enforced via the public constants on the class. A zero `sfLoanBrokerID` is unconditionally rejected here rather than waiting for a ledger lookup failure.

**`checkSign`** extends the base-class signature check with a second verification for the counterparty. The counterparty identity is resolved lazily: the transaction may provide `sfCounterparty` explicitly, or it falls back to reading the broker's `sfOwner` from the current ledger view. The actual cryptographic check — which supports both single signatures and multisignatures via the `sfCounterpartySignature` object — is then delegated back to `Transactor::checkSign` with the resolved identity.

**`calculateBaseFee`** prices the extra cryptographic work: each signer in the `sfCounterpartySignature` (whether a single signer or each member of a multisig quorum) adds one base fee unit on top of the normal transaction cost. This directly parallels how the base class charges for `sfSigners` in multisig transactions.

**`getValueFields`** returns a static list of `STNumber` fields — principal, origination fee, service fee, late payment fee, close payment fee — that must be representable without precision loss in the vault's asset type. This list is used in two places: `preclaim` checks coarse representability before computing the loan scale, and `doApply` re-checks after the final scale is known (IOU types, where the required decimal precision can push a value below the type's resolution, can fail the second check even if they pass the first).

**`preclaim`** performs ledger-state-dependent validation. A notable early guard checks that the arithmetic of the payment schedule cannot overflow `uint32_t` timestamps — the final grace period deadline is `startDate + (interval × total) + grace`, and if any intermediate value would exceed `std::numeric_limits<uint32_t>::max()`, the transaction is killed with `tecKILLED` before loading any objects. Later checks confirm the broker exists, that the vault has sufficient available assets, that the vault has not hit its asset maximum, and that neither the vault pseudo-account, the broker pseudo-account, the borrower, nor the broker owner is frozen for the loan asset.

**`doApply`** is where the loan comes into existence. It first recomputes `computeLoanProperties` (including the full amortization schedule: periodic payment, total value outstanding, management fees) and then validates that the interest component would not push the vault over its asset ceiling. It checks that the broker's `sfDebtTotal` would not exceed `sfDebtMaximum` and that available first-loss capital (`sfCoverAvailable`) still meets the `sfCoverRateMinimum` after the new loan is added — with the minimum cover rounded upward, deliberately erring on the side of the broker's solvency. Principal is disbursed from the vault pseudo-account with a single `accountSendMulti` call: `(principalRequested - originationFee)` goes to the borrower, and `originationFee` goes to the broker owner, both in one atomic operation. Holdings (trust lines or MPT holdings) for the borrower and broker owner are created on demand if absent. After the `Loan` SLE is inserted and all fields populated, the vault's `sfAssetsAvailable` and `sfAssetsTotal` are updated in tandem, the broker's `sfDebtTotal` and `sfLoanSequence` are incremented, and the loan is linked into both the broker pseudo-account's directory and the borrower's owner directory.

## Protocol Constants

The three public `constexpr` values encode the minimum viable loan configuration and the relationship between timing parameters:

- `minPaymentInterval = 60` and `defaultPaymentInterval = 60` establish one minute as the floor for payment cadence, preventing loans that fire faster than ledger close times can reliably track.
- `defaultGracePeriod = 60` must be at least `minPaymentInterval` — enforced by a `static_assert` — since a grace period shorter than the payment interval could produce negative-time schedules.
- `minPaymentTotal = 1` and `defaultPaymentTotal = 1` allow single-payment (balloon) loans as the degenerate case.

These constants are referenced directly in both `preflight` (for validation bounds) and `preclaim` (as defaults when optional fields are absent), so they function as the authoritative source of truth for the payment schedule floor throughout the lending protocol.