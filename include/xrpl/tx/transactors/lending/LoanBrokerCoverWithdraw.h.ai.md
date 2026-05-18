# `LoanBrokerCoverWithdraw` — Loan Broker Cover Withdrawal Transactor

## Role in the System

`LoanBrokerCoverWithdraw` is the transactor that lets a loan broker owner reclaim cover funds that are no longer needed. Within the XRPL lending protocol (XLS-66), a `LoanBroker` ledger object acts as an intermediary between lenders (vaults) and borrowers. To guarantee the vault against borrower defaults, the broker must maintain a reserve of *cover*: assets held in the broker's own pseudo-account. This transactor handles the controlled exit path — withdrawing cover back to the owner's account or to a named third-party destination, while enforcing the minimum cover ratio that must remain in place as long as outstanding loans exist.

The class lives in the `xrpl` namespace alongside its symmetric counterparts `LoanBrokerCoverDeposit` and `LoanBrokerCoverClawback`, all sharing the same structural contract and following the standard XRPL transactor pipeline.

## Transactor Architecture

`LoanBrokerCoverWithdraw` inherits from `Transactor` and participates in the three-phase execution model that every transactor follows:

1. **`checkExtraFeatures`** (static, preflight-gating): Delegates directly to `checkLendingProtocolDependencies(ctx)`, which validates that all required feature amendments for the lending protocol are enabled in the current ledger rules. If any dependency is absent, `invokePreflight` will return `temDISABLED` before any further validation occurs.

2. **`preflight`** (static, stateless validation): Performs purely syntactic checks against the serialized transaction fields without consulting the ledger. It rejects a zero `sfLoanBrokerID`, a non-positive or legally-invalid `sfAmount`, and a zero-value `sfDestination` if one is provided. Critically, `preflight` does *not* look up any ledger objects — that is reserved for `preclaim`.

3. **`preclaim`** (static, read-only ledger checks): Performs the bulk of business-rule validation against the current `ReadView`. It: resolves the destination account (defaulting to the submitter's own account if `sfDestination` is absent); rejects withdrawals targeting a pseudo-account; loads and validates the `LoanBroker` ledger object and confirms ownership; loads the associated `Vault` to discover the underlying asset; enforces asset-transfer invariants (transferability, freeze, deep-freeze, authorization); and finally calculates the minimum cover constraint.

4. **`doApply`** (instance, mutable ledger mutation): Decrements `sfCoverAvailable` on the `LoanBroker` SLE by the requested amount, calls `view().update(broker)`, invokes `associateAsset` to maintain asset-tracking bookkeeping, then delegates the actual token movement to the shared `doWithdraw` helper, passing the broker's pseudo-account as the source of funds.

`ConsequencesFactory` is set to `Normal`, meaning this transaction does not block unrelated transactions from the same account from being applied.

## Cover Minimum Enforcement

The most significant invariant enforced in `preclaim` is the minimum cover ratio. The broker stores `sfCoverRateMinimum` as a 32-bit tenth-bips value (units of 0.001 basis points). Given the broker's current `sfDebtTotal` — the total principal outstanding across all its loans — the minimum allowable cover is:

```
minimumCover = roundUp(tenthBipsOfValue(debtTotal, coverRateMinimum))
```

The `NumberRoundModeGuard` is deliberately set to `Number::upward` before this calculation so that the minimum requirement is always rounded conservatively: any fractional asset unit is rounded *up*, never truncated. After confirming that the withdrawal amount does not exceed `sfCoverAvailable`, the code then checks that `(coverAvail - amount) >= minimumCover`. A separate check against `accountHolds` on the pseudo-account provides a final guard against stale or inconsistent ledger state.

## Third-Party Destination and Authorization Model

The optional `sfDestination` field elevates the security requirements. When the destination is the same as the transaction submitter, only `WeakAuth` is required — the owner is reclaiming their own funds. When the destination is a third party, the withdrawal becomes effectively an asset transfer to an external account. In that case:
- `canWithdraw` is called to verify the submitting account holds any credentials or permissions needed for third-party asset movements.
- `authType` is upgraded to `StrongAuth`, meaning the destination account must have already consented to receive the asset by establishing a `RippleState` (for IOU assets) or an `MPToken` (for MPT assets).

This two-tier model prevents the broker owner from inadvertently or maliciously pushing assets into accounts that have not opted in, while still allowing simple self-withdrawals with a lighter authorization burden.

## Pseudo-Account Pattern

The actual cover funds do not sit directly in the broker owner's account. Instead, the `LoanBroker` SLE carries an `sfAccount` field pointing to a broker-specific *pseudo-account* — a synthetic account that holds the vault-asset balance. The owner interacts with cover through deposit and withdrawal transactions rather than direct transfers. `doApply` reads `brokerPseudoID` from the SLE and passes it to `doWithdraw` as the debit-side account, so the standard payment machinery debits the pseudo-account and credits the destination.

## Error Handling and Failure Modes

`preclaim` returns well-defined `TER` codes for each failure scenario: `tecPSEUDO_ACCOUNT` for a pseudo-account destination, `tecNO_ENTRY` for a missing broker, `tecNO_PERMISSION` for ownership mismatches, `tefBAD_LEDGER` (marked `LCOV_EXCL`) for the theoretically impossible case of a broker existing without its vault, `tecWRONG_ASSET` for an amount/asset type mismatch, and `tecINSUFFICIENT_FUNDS` for both the cover-availability check and the minimum-cover-ratio check. In `doApply`, the defensive re-checks of `broker` and `vault` presence return `tecINTERNAL` (also `LCOV_EXCL`) because `preclaim` guarantees their existence; those paths exist only to satisfy the type system rather than to recover from real failures.