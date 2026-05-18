# `LoanBrokerCoverWithdraw.cpp`

## Role in the System

This file implements the `LoanBrokerCoverWithdraw` transactor, the mechanism by which the owner of a `LoanBroker` ledger object recoups idle cover capital previously deposited into the broker's pseudo-account. It is the withdrawal leg of the `LoanBrokerCoverDeposit` / `LoanBrokerCoverWithdraw` pair that governs a broker's collateral pool in the XRPL lending protocol (XLS-66).

A `LoanBroker` intermediates between a lending vault and individual borrowers. Cover capital sits in a pseudo-account controlled by the broker and backstops outstanding loans. Because that capital is locked inside a pseudo-account, no ordinary payment transaction can reach it; this transactor is the only sanctioned path out.

## Transaction Flow

The code follows the standard XRPL three-phase transactor pattern: `checkExtraFeatures` → `preflight` → `preclaim` → `doApply`.

**`checkExtraFeatures`** does nothing beyond delegating to `checkLendingProtocolDependencies`, which gates the entire lending feature set on protocol amendments. If the required amendments are not enabled on the ledger, the transaction is rejected before any field parsing.

**`preflight`** performs cheap, stateless sanity checks. It rejects a zero `sfLoanBrokerID`, a non-positive or otherwise illegal `sfAmount`, and a zero `sfDestination` if one is supplied. The destination is optional — when absent the submitter withdraws to themselves.

**`preclaim`** is where the substantive enforcement happens. Its logic breaks into several layers:

1. **Object lookups and ownership.** The preclaim resolves the `LoanBroker` SLE by `keylet::loanbroker(brokerID)` and confirms the submitting account is its `sfOwner`. It then follows the broker's `sfVaultID` reference to the vault to obtain the canonical `sfAsset`. A missing vault is treated as `tefBAD_LEDGER` and marked `LCOV_EXCL_START` because in a well-formed ledger, the broker cannot outlive its vault.

2. **Asset compatibility.** The requested withdrawal amount's asset must match the vault's asset. A mismatch returns `tecWRONG_ASSET`, preventing type confusion.

3. **Transfer-path checks.** The source of funds is the broker's `sfAccount` pseudo-account, not the submitting account. `canTransfer` checks whether the asset is transferable from that pseudo-account to the destination. If the destination differs from the submitter, the transactor escalates to `StrongAuth` and calls `canWithdraw(ctx.view, tx)`, which enforces that the destination account exists, that it has not set a destination tag requirement that would block the transfer, that deposit authorization is satisfied, and that the destination will not exceed trustline or MPToken limits. The `AuthType` discrimination is intentional: withdrawing to yourself is a balance adjustment, but withdrawing to a third party is functionally equivalent to a transfer, warranting full transfer-path scrutiny.

4. **Freeze checks.** Unless the destination is the asset issuer (who is exempt from their own freeze restrictions), the broker's pseudo-account is checked for source-side freezes and the destination is checked for deep-freeze status. This mirrors the same pattern used in vault and payment transactors.

5. **Minimum cover enforcement.** This is the defining constraint unique to this transactor. After computing how much cover is currently available (`sfCoverAvailable`), preclaim calculates the minimum cover that must remain:

   ```cpp
   NumberRoundModeGuard const mg(Number::upward);
   minimumCover = roundToAsset(
       vaultAsset,
       tenthBipsOfValue(currentDebtTotal, TenthBips32(sleBroker->at(sfCoverRateMinimum))),
       currentDebtTotal.exponent());
   ```

   `sfCoverRateMinimum` is expressed in 1/10 basis-point units, so `tenthBipsOfValue` computes a fractional portion of the total outstanding debt. The `NumberRoundModeGuard` forces upward rounding for both the bips multiplication and the `roundToAsset` call, deliberately overstating the required minimum to prevent rounding exploitation. If the proposed withdrawal would leave `coverAvail - amount < minimumCover`, the transaction returns `tecINSUFFICIENT_FUNDS`. A separate check ensures the withdrawal does not exceed total cover available at all.

6. **Actual balance.** Even if the ledger's accounting says cover is available, the actual pseudo-account balance is verified with `accountHolds`. This guards against ledger inconsistencies where `sfCoverAvailable` and the real balance drift apart.

**`doApply`** executes with all checks satisfied:

```cpp
broker->at(sfCoverAvailable) -= amount;
view().update(broker);
associateAsset(*broker, vaultAsset);
return doWithdraw(view(), tx, account_, dstAcct, brokerPseudoID, preFeeBalance_, amount, j_);
```

`sfCoverAvailable` is decremented first, then `associateAsset` is called on the broker SLE. This is required because numeric fields on SLEs that hold asset-denominated `STNumber` values must be told which asset they represent before the ledger serializes them; failing to call it would corrupt the broker's stored numbers. The actual asset transfer is performed by the shared `doWithdraw` helper from `View.h`, which handles the specifics of moving funds from a pseudo-account (`brokerPseudoID`) to the destination, applying waived transfer fees and other settlement logic common to all vault-adjacent withdrawals.

## Design Decisions

**Pseudo-account as source, not submitter.** The submitting account (`account_`) authorizes the transaction but is never the source of funds — `brokerPseudoID` is. This cleanly separates custody from authorization and prevents the broker owner from "spending" cover directly from their own account.

**Conditional `StrongAuth` vs. `WeakAuth`.** Withdrawing to a third party demands `StrongAuth` (the destination must have a RippleState or MPToken already created), while withdrawing to self requires only `WeakAuth`. This avoids forcing broker owners to pre-establish trust relationships with themselves just to reclaim their own capital, while still enforcing full consent checks for novel recipients.

**Upward rounding for minimum cover.** The minimum cover calculation is explicitly wrapped in `NumberRoundModeGuard(Number::upward)`. This is a conservative choice: when the ledger rounds the minimum required cover up, withdrawals are blocked marginally sooner than they might be with neutral rounding, giving loans extra protection against edge-case under-collateralization from accumulated rounding dust.

**Mirror symmetry with `LoanBrokerCoverDeposit`.** The two transactors are intentional inverses. Deposit increments `sfCoverAvailable` after calling `accountSend` from the submitter to the pseudo-account; withdraw decrements it before calling `doWithdraw` from the pseudo-account to the destination. Both call `associateAsset` to keep STNumber fields correctly typed throughout.