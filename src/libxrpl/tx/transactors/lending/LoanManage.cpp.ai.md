# `LoanManage.cpp` — Loan Lifecycle State Transitions

`LoanManage.cpp` implements the `LoanManage` transactor, which manages the credit-quality lifecycle of a loan object in the XRPL lending protocol (XLS-66). While `LoanSet` creates loans and `LoanPay` processes payments, `LoanManage` handles the three adversarial state transitions a loan broker needs when a borrower misses payments: marking a loan as impaired, clearing that impairment, or triggering a formal default. Because these transitions involve multi-object accounting across the `Loan`, `LoanBroker`, and `Vault` ledger entries, the transactor exists as a dedicated type rather than being folded into `LoanPay`.

## Transaction Flags as a State Machine

A `LoanManage` transaction carries exactly one of three optional flags — `tfLoanDefault`, `tfLoanImpair`, `tfLoanUnimpair` — or no flag at all (a deliberate no-op). The `preflight` method enforces mutual exclusivity with a standard power-of-two bitmask trick: if more than one bit in `*flagField & tfUniversalMask` is set, `(flags & (flags - 1)) != 0` is true and the transaction is rejected with `temINVALID_FLAG`.

The underlying state machine is directional and is enforced in `preclaim`:

- **Normal → Impaired** (`tfLoanImpair`): records a paper loss on the vault.
- **Normal → Default** (`tfLoanDefault`): finalizes the loss and triggers First-Loss Capital recovery.
- **Impaired → Normal** (`tfLoanUnimpair`): reverses the paper loss and resets the payment schedule.
- **Impaired → Default** (`tfLoanDefault`): same final settlement path as above.
- **Default → anything**: permanently blocked; a defaulted loan is a terminal state.
- **Re-impair an already-impaired loan**: blocked.
- **Unimpair a normal loan**: blocked.

`preclaim` also enforces two other preconditions: a fully-paid loan (`sfPaymentRemaining == 0`) cannot be modified, and a default cannot be triggered before `sfNextPaymentDueDate + sfGracePeriod` has elapsed, returning `tecTOO_SOON` if attempted too early. Authorization is strictly gated: the transaction must be submitted by the account that owns the `LoanBroker` associated with the loan — not the borrower, not the vault owner.

## `owedToVault`: A Key Accounting Identity

The file-local helper `owedToVault()` encodes the spec formula from XLS-66 §3.2.3.2. A vault is only owed principal and its portion of interest; management fees accrue to the broker. Since `sfInterestOutstanding` is not stored directly (it would be redundant), the identity is:

```
owedToVault = sfTotalValueOutstanding - sfManagementFeeOutstanding
```

This value drives both the impairment "paper loss" and the default settlement amount, making it a load-bearing calculation used in all three operation paths.

## `impairLoan`: Marking a Paper Loss

When a loan is impaired, the vault records an unrealized loss equal to `owedToVault(loanSle)`. The `sfLossUnrealized` field on the vault SLE is incremented via `adjustImpreciseNumber`, which rounds the result to the vault's asset scale and clamps to zero to prevent accumulated floating-point dust from going negative. A guard checks that the resulting unrealized loss does not exceed the vault's unavailable assets (`sfAssetsTotal - sfAssetsAvailable`); exceeding that would leave the vault in an arithmetically inconsistent state and returns `tecLIMIT_EXCEEDED`.

One subtlety: if the loan's next payment due date hasn't passed yet, `impairLoan` accelerates it to the current ledger close time. This signals that payment is immediately expected; the grace period clock starts from now rather than from the original schedule.

The impaired flag `lsfLoanImpaired` is set on the loan object, but no funds move. This is a pure accounting mark.

## `unimpairLoan`: Reversing the Paper Loss

`unimpairLoan` is the mirror operation. It decrements `sfLossUnrealized` by the same `owedToVault` amount and clears `lsfLoanImpaired`. Restoring the next payment due date requires a policy decision: if the originally scheduled due date is still in the future, restore it; otherwise, set it to `now + paymentInterval`. This prevents a restored loan from being immediately overdue simply because the impairment period consumed the original window.

## `defaultLoan`: First-Loss Capital Settlement

`defaultLoan` is the most financially complex operation. It settles the outstanding balance by drawing on First-Loss Capital held by the `LoanBroker` pseudo-account before writing the remainder off against the vault.

The First-Loss Capital coverage amount is computed in two rounds using `NumberRoundModeGuard` set to `Number::upward` to protect vault depositors:

1. `minimumCover = coverRateMinimum × sfDebtTotal` (the broker's total outstanding debt scaled by the minimum coverage rate).
2. `covered = min(minimumCover × coverRateLiquidation, totalDefaultAmount)` then clamped to `sfCoverAvailable`.

The rates are `TenthBips32` values — one ten-thousandth of a basis point — allowing fine-grained coverage ratios. The vault absorbs `totalDefaultAmount - defaultCovered` as a realized loss, rounded downward (`Number::downward`) to the vault's own scale to again favor depositors.

The code includes a deliberate dust-handling edge case (lines 192–208): because the loan and vault may operate at different decimal scales, subtracting `vaultDefaultRounded` from total assets and simultaneously adding back `defaultCovered` can leave `sfAssetsAvailable` fractionally above `sfAssetsTotal` by a rounding artifact. The guard checks whether the exponent difference between the two values exceeds 13 orders of magnitude; if so, the difference is classified as dust and the total is bumped up to match the available amount. This prevents a fatal consistency violation from a legitimate precision mismatch.

After accounting, the loan is zeroed out (`sfTotalValueOutstanding`, `sfPrincipalOutstanding`, `sfManagementFeeOutstanding`, `sfPaymentRemaining`, `sfNextPaymentDueDate` all set to 0), `lsfLoanDefault` is set, and `accountSend` moves the covered amount from the broker's pseudo-account to the vault's pseudo-account with `WaiveTransferFee::Yes`. This is necessary because the broker pseudo-account holds the First-Loss Capital as an on-chain balance.

If the loan was already impaired before defaulting, `sfLossUnrealized` on the vault is also decremented by `totalDefaultAmount`, converting the paper loss into the realized loss that was just recorded. A consistency check (`vaultLossUnrealizedProxy < totalDefaultAmount → tefBAD_LEDGER`) guards this path, though it is marked `LCOV_EXCL_LINE` as theoretically unreachable in a valid ledger.

## `doApply` and Amendment Gating

`doApply` resolves the chain `Loan → LoanBroker → Vault` by following `sfLoanBrokerID` on the loan and `sfVaultID` on the broker, then dispatches to the appropriate operation based on the transaction flag. Missing any SLE at this stage returns `tefBAD_LEDGER` rather than a `tec` code, because these objects must have been present in `preclaim` — any absence here indicates an internal ledger inconsistency.

After a successful operation, when the `fixSecurity3_1_3` amendment is active, `associateAsset` is called on all three SLEs. Pre-amendment, this call only happened on the no-op path. The amendment corrects this oversight so that asset index associations are maintained consistently across all `LoanManage` outcomes.

## Defensive Patterns

Every "impossible" state (broker SLE missing, vault SLE missing, cover available less than covered amount, unrealized loss less than default amount) is guarded with a `JLOG(j.warn())`/`JLOG(j.fatal())` followed by `tefBAD_LEDGER` or `tecINTERNAL`, all tagged `LCOV_EXCL_LINE`. This is the standard XRPL pattern: guard corrupted-ledger states with non-crashing error returns rather than assertions, so a validator encountering an unexpected ledger state fails the transaction cleanly rather than crashing the node.