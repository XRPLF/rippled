# `LoanBrokerSet.cpp` — LoanBroker Create/Update Transactor

## Role in the System

`LoanBrokerSet.cpp` implements the `LoanBrokerSet` transaction type, which is the entry point for managing `LoanBroker` ledger objects in the XRPL lending protocol (XLS-66). A `LoanBroker` sits between a `Vault` (the pooled liquidity provider) and individual `Loan` objects (borrowers), acting as the economic policy controller for a lending program: it defines the management fee structure, the collateral cover thresholds, and a total debt ceiling.

The transaction is dual-purpose. The same type handles both creation and modification, distinguished entirely by the presence of `sfLoanBrokerID` in the transaction fields. When absent, a new broker is created and linked to an existing vault. When present, only the mutable subset of fields may be updated. This design avoids introducing a separate `LoanBrokerCreate` / `LoanBrokerModify` pair, keeping the protocol surface compact at the cost of some conditional logic in every phase.

## Transaction Pipeline

Like all XRPL transactors, `LoanBrokerSet` participates in a three-phase pipeline:

**`checkExtraFeatures`** delegates directly to `checkLendingProtocolDependencies`, ensuring the relevant amendment gates are active before any field parsing begins.

**`preflight`** performs stateless validation of all transaction fields in order of increasing complexity. It validates numeric ranges for `sfManagementFeeRate`, `sfCoverRateMinimum`, `sfCoverRateLiquidation`, and `sfDebtMaximum` using the `Lending::validNumericRange` helper, and checks `sfData` size against `maxDataPayloadLength`. Two business-logic invariants are enforced here:

1. If `sfLoanBrokerID` is present (update mode), the immutable fields `sfManagementFeeRate`, `sfCoverRateMinimum`, and `sfCoverRateLiquidation` must not appear. These parameters define the economic terms seen by all borrowers under this broker and may not be retroactively changed — enforcing this at the stateless layer means no ledger access is needed to reject such attempts.

2. `sfCoverRateMinimum` and `sfCoverRateLiquidation` must either both be zero or both be non-zero. The two rates form a meaningful pair representing the minimum collateral ratio and the liquidation threshold respectively; having one without the other would leave the cover system in an undefined configuration.

**`preclaim`** performs ledger-state validation. It confirms the target vault exists, that the submitting account owns it, and — in update mode — that the referenced broker exists, belongs to the same vault, and is also owned by the submitter. The cross-checking against `sfVaultID` on the existing broker is the immutability guard for the vault association: a broker is permanently bound to the vault it was created for.

A notable business rule in `preclaim`: when updating `sfDebtMaximum`, the new value must not be below the broker's current `sfDebtTotal`, unless the new value is zero. A zero `sfDebtMaximum` means "unlimited", so reducing an existing cap to unlimited is always allowed. This prevents an owner from stranding active loans in a state where the outstanding debt already exceeds the new ceiling.

`preclaim` also calls `canAddHolding` and `checkFrozen` on the vault's pseudo-account in creation mode. These checks confirm the asset type (IOU/MPToken/XRP) can accept a new holding entry and that the vault's pseudo-account is not frozen — preconditions for the `addEmptyHolding` call that follows in `doApply`.

## Asset Precision Validation via `getValueFields()`

`getValueFields()` returns a static list containing only `sfDebtMaximum`. During `preclaim`, each field in this list is checked by constructing an `STAmount{asset, *value}` round-trip: if the value cannot be exactly represented as the vault's asset type, the transaction fails with `tecPRECISION_LOSS`. This matters primarily for MPToken and XRP amounts, which are integers — a `debtMaximum` that cannot be expressed in whole tokens would silently lose meaning after rounding.

## `doApply` — Object Creation

When no `sfLoanBrokerID` is present, `doApply` constructs the full broker object and its on-chain infrastructure:

1. A new `SLE` is allocated via `keylet::loanbroker(account_, sequence)`, where `sequence` is the transaction's sequence value. This makes the key deterministic and collision-resistant.

2. `dirLink` is called twice: once to insert the broker into the owner account's directory (using the default `sfOwnerNode`), and once to insert it into the vault's pseudo-account directory using `sfVaultNode`. The two-way directory linkage allows ledger traversal tools and deletion logic to efficiently enumerate all brokers associated with either an owner or a vault.

3. `adjustOwnerCount` increments the owner's reserve count by two — one slot for the broker SLE and one for the pseudo-account that will be created next. The reserve sufficiency check (`preFeeBalance_ < view.fees().accountReserve(ownerCount)`) occurs *after* the increment, so the test reflects the final post-creation reserve requirement.

4. `createPseudoAccount` creates a synthetic `AccountRoot` SLE keyed from `broker->key()`, with `sfLoanBrokerID` as the back-reference field type. This gives the broker an on-chain identity capable of holding assets, particularly for cover deposits made by lenders or third parties.

5. `addEmptyHolding` initializes the pseudo-account's trust relationship with the vault's asset type (a trust line for IOUs, an MPToken slot for MPTokens, or nothing for XRP). This is necessary before any asset can flow into or out of the broker's pseudo-account.

6. The broker's `sfLoanSequence` is initialized to 1. Each loan subsequently created under this broker will consume a sequence number, providing loan objects with a stable, broker-scoped identifier.

## `doApply` — Object Update

When `sfLoanBrokerID` is present, only `sfData` and `sfDebtMaximum` may be patched — the code uses `~sfField` (optional-field accessors) so absent fields are simply ignored. After mutation, `view.update(broker)` commits the change. The vault SLE is read to retrieve `vaultAsset`, which is passed to `associateAsset`.

## `associateAsset` Convention

Both the create and update paths end with `associateAsset(*broker, vaultAsset)`. This function iterates the broker SLE's `STTakesAsset` and `STNumber` fields and calls their virtual `associateAsset` method, which re-rounds stored numeric values to the precision implied by the asset type. The XRPL convention documented in `STTakesAsset.h` is that this call must happen at the very end of `doApply`, after all writes are complete, to avoid cumulative rounding errors.

## Defensive Coding Patterns

Several paths in `doApply` include `LCOV_EXCL_START`/`LCOV_EXCL_STOP` guards around `tefBAD_LEDGER` returns. These represent states that `preclaim` has already ruled out: a broker not existing after `preclaim` confirmed its existence, or a vault disappearing between phases. The guards acknowledge that the code is unreachable in correct operation but exists as a safety net against future refactoring that could accidentally break the `preclaim`/`doApply` sequencing guarantee.