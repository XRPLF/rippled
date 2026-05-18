# `LoanBrokerSet.h` — LoanBroker Create/Update Transactor

## Role in the System

`LoanBrokerSet` implements the XRPL transaction type responsible for creating and modifying `LoanBroker` ledger objects within the XLS-66 on-chain lending protocol. Following the ledger's "set" (upsert) convention, a single transaction type handles both the initial creation of a broker and subsequent updates to its mutable fields, discriminated at runtime by whether `sfLoanBrokerID` is present in the transaction.

A `LoanBroker` is a protocol actor that intermediates between a `Vault` (the liquidity pool) and borrowers. Each broker is permanently associated with exactly one vault, earns a management fee from loan interest, and optionally enforces collateral cover-rate thresholds that trigger liquidation. `LoanBrokerSet` is how the vault owner provisions and reconfigures such a broker.

## Class Structure

`LoanBrokerSet` inherits from `Transactor` and follows the three-phase validation model common to all XRPL transactors:

```
checkExtraFeatures → preflight → preclaim → doApply
```

`ConsequencesFactory` is set to `Normal`, which means the transaction cannot block the account's sequence from advancing — it is a standard, displaceable operation.

`checkExtraFeatures()` delegates entirely to `checkLendingProtocolDependencies()`, gating the entire lending feature set on its required amendments being enabled in the current rule set. This is consistent with every other transactor in the `lending/` subdirectory, ensuring that all lending operations share a common amendment check.

## `preflight()` Validation

The preflight phase validates fields that can be checked without ledger access:

- **`sfData`**: Optional metadata blob. If present, its length is capped at `maxDataPayloadLength`.
- **Rate fields** (`sfManagementFeeRate`, `sfCoverRateMinimum`, `sfCoverRateLiquidation`): Each is independently validated against a protocol-defined maximum via `validNumericRange()`.
- **`sfDebtMaximum`**: Validated to lie within `[0, maxMPTokenAmount]` — the upper bound matches the maximum for MPToken amounts, covering the case where the vault asset is an MPToken.
- **Mutability constraint**: When `sfLoanBrokerID` is present (update mode), the three "fixed" fields — `sfManagementFeeRate`, `sfCoverRateMinimum`, `sfCoverRateLiquidation` — are rejected. These parameters are set once at creation and cannot be renegotiated after loans may be outstanding against them.
- **Cover-rate pairing invariant**: `sfCoverRateMinimum` and `sfCoverRateLiquidation` must either both be zero or both be non-zero. A minimum with no liquidation threshold (or vice versa) is incoherent and is rejected with `temINVALID`.

## `getValueFields()` and Precision Checking

`getValueFields()` returns a static `vector` containing `~sfDebtMaximum` (the optional `sfDebtMaximum` field). This is used in `preclaim()` to verify that the requested `sfDebtMaximum` value can be faithfully represented in the vault's asset type — important for non-IOU assets like XRP (integer drops) or MPTokens, where a fractional amount would silently lose precision. If `STAmount{asset, *value} != *value`, the transaction fails with `tecPRECISION_LOSS`. The static vector design avoids repeated heap allocation across calls.

`LoanSet` declares the same `getValueFields()` pattern, indicating this is a protocol-level convention for numeric fields that must round-trip cleanly through the ledger's asset representation.

## `preclaim()` Validation

`preclaim()` has read access to the ledger and enforces ownership and consistency rules:

- The referenced `sfVaultID` must resolve to an existing vault, and the transaction's `sfAccount` must match `sfOwner` on that vault. Only the vault owner can create or modify brokers against it.
- **In update mode** (`sfLoanBrokerID` present): The broker object must exist, its `sfVaultID` must match the transaction's `sfVaultID` (vault association is immutable), and `sfAccount` must own the broker. A guard prevents setting `sfDebtMaximum` to a non-zero value below the broker's current `sfDebtTotal`, which would strand outstanding loans above the new limit.
- **In creation mode**: `canAddHolding()` confirms the vault can accept a new trust line / holding, and `checkFrozen()` ensures the vault's pseudo-account is not frozen for the asset. These checks are skipped in update mode because the holding was already established at creation.

## `doApply()` — Create vs. Update

**Update path**: Only `sfData` (metadata) and `sfDebtMaximum` are written back to the existing broker SLE; then `view.update()` commits the change. Rate fields are deliberately absent here, enforcing the immutability constraint at apply time as a second guard.

**Create path**: This is the more complex code path:

1. The broker's `keylet` is derived from `(account_, sequence)` — making it unique per account and transaction sequence.
2. Two directory entries are created via `dirLink()`: one in the owner's account directory, and one in the vault's pseudo-account directory under `sfVaultNode`. The latter link allows the vault to enumerate all its brokers.
3. `adjustOwnerCount()` increments the owner count by **two** — one for the broker SLE and one for the broker's own pseudo-account — and the reserve check is deferred until after the increment, intentionally using `preFeeBalance_` (balance before the fee was deducted) to test the strict pre-fee balance.
4. A pseudo-account is created via `createPseudoAccount()` keyed on the broker's ledger key, with `sfLoanBrokerID` as the back-reference type. Pseudo-accounts give the broker an on-chain identity for holding collateral assets.
5. `addEmptyHolding()` establishes the broker's trust line / MPToken holding for the vault asset on the pseudo-account.
6. All fields (`sfSequence`, `sfVaultID`, `sfOwner`, `sfAccount`, `sfLoanSequence`, optional fields) are initialized. `sfLoanSequence` starts at `1` and is used by `LoanSet` to index loans issued through this broker.

`associateAsset()` is called in both paths to register the broker's relationship to the vault asset in any asset-specific indexes.

## Relationship to Sibling Transactors

The lending subdirectory contains a coherent set of transactors: `LoanBrokerSet` (this file), `LoanBrokerDelete`, `LoanBrokerCoverDeposit/Withdraw/Clawback`, `LoanSet`, `LoanManage`, `LoanPay`, and `LoanDelete`. `LoanBrokerSet` establishes the broker entity that the rest of these transactors operate on. Its `sfLoanSequence` counter is consumed by `LoanSet` when opening new loans, tying the lifecycle of individual loans to their originating broker.