# `LoanBrokerCoverClawback.cpp`

## Role in the Lending Protocol

The XRPL lending protocol (XLS-66) introduces a `LoanBroker` ledger object that acts as an intermediary between a single-asset vault and individual loans. Each broker maintains a "cover" pool — assets deposited into the broker's pseudo-account — that serves as a first-loss buffer absorbing borrower defaults before vault depositors are affected. `LoanBrokerCoverClawback.cpp` implements the transaction that lets the original asset issuer reclaim a portion of these cover funds, subject to a configurable minimum cover-to-debt ratio.

This is the inverse of `LoanBrokerCoverDeposit`. Comparing the two files shows their structural symmetry: deposit increments `sfCoverAvailable` and sends funds *to* the pseudo-account; clawback decrements `sfCoverAvailable` and sends funds *from* the pseudo-account, using `WaiveTransferFee::Yes` in both directions.

## Dual Identification Modes

One of the more unusual design choices here is that the transaction has two ways to identify the target broker:

1. **Explicit**: provide `sfLoanBrokerID` directly.
2. **Implicit**: provide `sfAmount` as an IOU where the `issuer` field encodes the broker's pseudo-account. The ledger-side `determineBrokerID()` helper resolves this by reading the pseudo-account SLE and extracting its `sfLoanBrokerID` field.

This dual mode exists because IOU trust lines are bidirectional — both endpoints are simultaneously "issuer" and "holder" from the protocol's perspective. A user might naturally express "claw back 100 USD from the broker's pseudo-account" by constructing an IOU amount with the pseudo-account as the issuer, mimicking normal IOU clawback syntax. The implicit path accommodates that convention. `preflight` rejects the combination of implicit-mode with `MPTIssue`, since MPTs lack the trust-line issuer encoding that makes implicit resolution possible.

The `determineAsset()` helper normalizes the resulting asset representation: whether the submitter specifies the IOU from their own account's perspective or from the pseudo-account's perspective, the function always returns an `Issue` with the submitting account as the issuer, matching the canonical vault asset representation used in comparisons against `sfAsset`.

## Minimum Cover Enforcement

`determineClawAmount()` is the financial heart of the file. It computes the maximum permissible withdrawal as:

```
minRequiredCover = tenthBipsOfValue(sfDebtTotal, sfCoverRateMinimum)  [rounded UP]
maxClawAmount   = sfCoverAvailable - minRequiredCover                 [rounded DOWN]
```

The deliberate asymmetric rounding — ceiling for the minimum required, floor for the remainder — ensures the ledger never allows cover to fall below the minimum ratio. `NumberRoundModeGuard` scopes the rounding mode changes, restoring the previous mode on exit. If the broker's cover is already at or below its minimum, `determineClawAmount` returns `tecINSUFFICIENT_FUNDS`.

When `sfAmount` is absent or zero, the convention is "take all you can" — the function caps to `maxClawAmount`. When a specific amount is requested, it is silently capped to `maxClawAmount` rather than rejected; the submitter receives at most what the minimum-ratio floor allows, without having to know the exact number in advance.

## Asset-Type-Specific Permission Checks

`preclaim` uses a `std::visit` dispatch to call one of two template specialisations of `preclaimHelper`:

- `preclaimHelper<Issue>` enforces that the issuer account has `lsfAllowTrustLineClawback` set and does *not* have `lsfNoFreeze` set. This mirrors the standard IOU clawback permission model.
- `preclaimHelper<MPTIssue>` looks up the `MPTIssuance` SLE and checks `lsfMPTCanClawback`. It also asserts that the issuance's recorded `sfIssuer` matches the submitting account — a redundant internal consistency check marked `LCOV_EXCL_LINE`.

XRP is explicitly blocked at both `preflight` (via `amount->native()`) and `preclaim` (via `vaultAsset.native()`), because native assets have no counterparty and therefore nobody can exercise a trust-line clawback.

Only the vault asset's issuer is authorized to clawback. The check `vaultAsset.getIssuer() != account` in `preclaim` ensures this, short-circuiting before any asset-type dispatch.

## Balance Invariant Check

A notable defensive check in `preclaim` explicitly verifies that the broker pseudo-account's actual trust-line or MPT balance (via `accountHolds`) is at least as large as the computed `clawAmount`. Ordinarily `sfCoverAvailable` and the on-ledger balance should be perfectly synchronized by the deposit and withdraw paths. The check exists to detect any ledger corruption that might cause them to diverge. If they do, `tecINTERNAL` is returned rather than allowing the transaction to proceed on potentially inconsistent state.

## `doApply` Execution

After `preclaim` has validated everything, `doApply` is deliberately minimal. It re-runs `determineBrokerID` and `determineClawAmount` (since they operate on the live mutable view rather than the read-only preclaim view), decrements `sfCoverAvailable` on the broker SLE, calls `view().update()` to commit that change, invokes `associateAsset` to maintain the broker's asset-type metadata index, then calls `accountSend` to transfer funds from the pseudo-account to the submitting account with the transfer fee waived. Every `tecINTERNAL` path in `doApply` is marked `LCOV_EXCL_LINE` — they are structurally unreachable because `preclaim` already established the necessary preconditions on the same ledger state.

## Relationship to Sibling Files

This file is one of three that manage the cover pool lifecycle: `LoanBrokerCoverDeposit.cpp` (owner adds cover), `LoanBrokerCoverWithdraw.cpp` (owner removes cover), and this file (asset issuer claws back cover). The deposit and withdraw paths are restricted to the broker's `sfOwner`; only this clawback path is exercised by the third-party asset issuer, reflecting the regulatory clawback rights attached to IOU/MPT issuance on XRPL.