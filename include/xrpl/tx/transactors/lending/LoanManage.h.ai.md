# `LoanManage.h` — Loan Status Management Transactor

## Role in the System

`LoanManage` is a transactor in the XRPL lending protocol (XLS-66 specification) responsible for transitioning a loan through its three credit-quality states: **unimpaired**, **impaired**, and **defaulted**. It sits in the `lending` transactor subdirectory alongside `LoanSet`, `LoanPay`, `LoanDelete`, `LoanBrokerSet`, and the cover-operation transactors, forming the lifecycle management layer of the protocol.

While `LoanSet` originates a loan and `LoanPay` processes borrower payments, `LoanManage` is the mechanism by which the loan broker signals that a borrower is struggling to repay. Its authority is strictly limited to the owner of the `LoanBroker` object that issued the loan — no other party can trigger these state transitions.

## Transactor Inheritance and Lifecycle Hooks

`LoanManage` inherits from `Transactor` using the static-polymorphism pattern common throughout the XRPL codebase. Rather than virtual dispatch, `Transactor::invokePreflight<T>()` calls into `T::checkExtraFeatures`, `T::getFlagsMask`, `T::preflight`, and `T::preflightSigValidated` by name, enforcing a compile-time protocol. `LoanManage` overrides all the relevant hooks:

- **`checkExtraFeatures`** delegates to `checkLendingProtocolDependencies`, which gates the entire transactor on the amendment(s) required by the lending protocol. This is the correct place for amendment checks — the base class `invokePreflight` template returns `temDISABLED` if `checkExtraFeatures` returns false.
- **`getFlagsMask`** returns `tfLoanManageMask`, restricting which transaction flags are legal. The mask covers exactly the three action flags: `tfLoanDefault`, `tfLoanImpair`, and `tfLoanUnimpair`.
- **`preflight`** validates two things: that a non-zero `LoanID` is present, and that the three action flags are mutually exclusive. The mutual-exclusivity check uses the standard bit-twiddling idiom `(flags & (flags - 1)) != 0` to detect whether more than one bit is set.
- **`preclaim`** enforces the **loan state machine** before touching mutable state: a defaulted loan is permanently frozen; an already-impaired loan cannot be impaired again; an unimpaired loan cannot be unimpaired. Additionally, defaulting is only legal after the next payment due date plus grace period has expired, preventing premature default triggers. `preclaim` also verifies that the submitting account owns the `LoanBroker` associated with the loan.

`ConsequencesFactory` is set to `Normal`, meaning the transaction fee is claimed even on failure in the standard way — appropriate since this transaction cannot block the queue.

## The Loan State Machine

The impairment lifecycle follows a strict directed acyclic graph:

```
unimpaired  ──►  impaired  ──►  defaulted
     └─────────────────────────────►
                  impaired  ◄──  unimpaired (recovery)
```

Once a loan enters `lsfLoanDefault`, `preclaim` permanently prevents any further modification. A fully-paid loan (`sfPaymentRemaining == 0`) is also immutable through this transactor. These guards are checked in `preclaim` rather than `doApply`, so invalid transactions claim a fee but do not touch ledger state.

## Static Helper Methods and Cross-Transactor Reuse

The three core operations — `defaultLoan`, `impairLoan`, and `unimpairLoan` — are exposed as `public static` methods with `/** Helper function that might be needed by other transactors */` comments. This design acknowledges that other transactors (e.g., a cover-clawback or broker-delete operation) may need to trigger the same ledger mutations as part of their own logic, avoiding code duplication while keeping the accounting logic in one place.

### `defaultLoan`

This is the most complex operation, implementing XLS-66 spec section 3.2.3.2. When a loan defaults:

1. **First-Loss Capital absorption**: The broker's cover capital (pledged as collateral) absorbs a portion of the loss. The absorbed amount is capped by two rates — `sfCoverRateMinimum` and `sfCoverRateLiquidation` (both in tenth-basis-points) — and further capped by `sfCoverAvailable`. The rounding mode is set to `upward` for the minimum required coverage, ensuring the broker always covers at least the required minimum.

2. **Vault accounting**: The vault's `sfAssetsTotal` decreases by the unabsorbed default amount (rounded down to the vault's asset scale to avoid inflating the vault). The vault's `sfAssetsAvailable` increases by the `defaultCovered` amount, since first-loss capital is returned as liquid assets. A dust-tolerance check handles the case where floating-point imprecision makes `sfAssetsAvailable` fractionally exceed `sfAssetsTotal` — if the difference exponent is more than 13 places smaller, it is treated as rounding dust and both values are equalized upward.

3. **Unrealized loss reconciliation**: If the loan was previously impaired (which records a "paper loss" in `sfLossUnrealized`), that paper loss is cleared since the loss is now realized.

4. **Loan zeroing**: All outstanding balances (`sfTotalValueOutstanding`, `sfPrincipalOutstanding`, `sfManagementFeeOutstanding`, `sfPaymentRemaining`, `sfNextPaymentDueDate`) are set to zero and `lsfLoanDefault` is set.

5. **Pseudo-account transfer**: `accountSend` moves the covered amount from the broker's pseudo-account back to the vault's pseudo-account with `WaiveTransferFee::Yes`, since this is an internal settlement, not a user-initiated transfer.

### `impairLoan`

`impairLoan` marks a loan as troubled without yet realizing the loss. It records the full amount owed to the vault (`sfTotalValueOutstanding - sfManagementFeeOutstanding`) as `sfLossUnrealized` in the vault — a "paper loss" that reduces the effective NAV of the vault's shares without moving any funds. If the next payment due date has not yet passed, it is advanced to the current ledger close time, accelerating the payment schedule. There is a guard: if the unrealized loss would exceed the vault's unavailable assets (total minus available), the operation is rejected with `tecLIMIT_EXCEEDED` to prevent the vault from entering an inconsistent state.

### `unimpairLoan`

`unimpairLoan` reverses an impairment. It clears the `sfLossUnrealized` entry in the vault (reversing the paper loss) and restores the `sfNextPaymentDueDate`. The restored date depends on timing: if the original next payment date has not yet passed, the loan returns to its normal amortization schedule; otherwise the due date is reset from the current ledger time plus one payment interval, giving the borrower a full interval to make the next payment. `unimpairLoan` is marked `[[nodiscard]]` to force callers to check its `TER` return — this is the only one of the three operations with this annotation, reflecting that its accounting rollback must not be silently ignored.

## `doApply` and Amendment Gating

`doApply` resolves the loan, broker, and vault SLEs via `view.peek()` (obtaining mutable references), then dispatches to the appropriate static helper based on the transaction flag. No flags is explicitly valid — a documented no-op. After the flag dispatch, a secondary check for amendment `fixSecurity3_1_3` calls `associateAsset` on all three SLEs. This amendment-gated call was added as a correctness fix; before the amendment, `associateAsset` was only called on the no-op path, which was a bug for the state-changing paths.

## Invariants and Failure Modes

Ledger consistency failures that "should be impossible" (unreachable in correct ledger state) return `tefBAD_LEDGER` or `tecINTERNAL` marked with `LCOV_EXCL_LINE`, indicating they are excluded from coverage requirements. This is a common defensive pattern in the XRPL codebase: guard impossible states with fatal-level log messages and appropriate error codes rather than assertions, so a corrupted ledger does not crash a validator node.