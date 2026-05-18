# `VaultInvariant.h` — Post-Transaction Consistency Guard for Single Asset Vaults

## Role in the System

`VaultInvariant.h` declares the `ValidVault` invariant checker, one of the entries in the `InvariantChecks` tuple registered in `InvariantCheck.h`. The invariant framework runs every registered checker against every successful transaction before committing it to the ledger. `ValidVault` is the dedicated guardian for the Single Asset Vault feature (`featureSingleAssetVault`): it ensures that the ledger objects backing a vault — the `ltVAULT` entry, its companion `ltMPTOKEN_ISSUANCE` for share tracking, and the various account/trust-line/MPToken entries affected by deposits and withdrawals — remain internally consistent after any transaction that touches them.

## Two-Phase Architecture

Like all checkers in the framework, `ValidVault` works in two phases driven by the two public methods.

**`visitEntry`** is called once for each ledger entry touched by the transaction — before modification, after modification, or both. It collects snapshots into four private vectors (`beforeVault_`, `afterVault_`, `beforeMPTs_`, `afterMPTs_`) and, for balance-carrying objects (account roots, trust lines, `ltMPTOKEN`, `ltMPTOKEN_ISSUANCE`), accumulates a signed balance delta per ledger-object key into `deltas_`. The sign convention is non-obvious: `+1` for MPTokenIssuance (`sfOutstandingAmount` — decreasing this means shares are being minted to holders) and `-1` for individual balances (account root XRP, trust-line balance, MPToken amount), so that `deltas_[key] = balanceDelta * sign` yields a number whose sign represents whether the balance from the perspective of the vault's counterparty increased or decreased.

A subtle but important design note is that during `visitEntry` there is no way to tell whether a modified `ltMPTOKEN_ISSUANCE` belongs to the vault being processed or is some unrelated MPT issuance; both are recorded into `beforeMPTs_` / `afterMPTs_` and resolved lazily in `finalize` by matching `shareMPTID` against the vault entry.

**`finalize`** performs all the actual invariant assertions. It first checks whether the feature is enabled — storing the result in `enforce` — and uses this to decide between failing hard (return `false`) or soft-failing (returning `true` while firing a debug-only `XRPL_ASSERT`). This dual-mode pattern exists because invariant violations are expected during unit tests that deliberately disable the amendment, but must be fatal on mainnet.

## Nested Data Structures

`Vault` and `Shares` are private `final` structs with factory methods `Vault::make(SLE const&)` and `Shares::make(SLE const&)`. They extract and cache only the fields needed for the invariant checks — `assetsTotal`, `assetsAvailable`, `assetsMaximum`, `lossUnrealized`, `shareMPTID`, `pseudoId`, etc. — so that `finalize` can compare before/after values without re-reading ledger objects. Keeping them as aggregates with static factories rather than constructors makes the code straightforward to extend and avoids implicit conversion surprises.

## Key Invariants Enforced

`finalize` branches on `tx.getTxnType()` and applies a different set of rules for each vault-related transaction type:

- **`ttVAULT_CREATE`**: the vault must be new (no `beforeVault_`), all balance fields must be zero, the `ltMPTOKEN_ISSUANCE` must exist, its issuer must be a pseudo-account, and that pseudo-account must carry a `sfVaultID` back-reference pointing at the newly created vault's key. This cross-referencing check catches any scenario where the pseudo-account linkage is broken.

- **`ttVAULT_DELETE`**: the vault must be gone (`afterVault_` is empty), the matching share issuance must also have been deleted in the same transaction, and both `sharesTotal` and `assetsTotal` must be zero at the time of deletion.

- **`ttVAULT_DEPOSIT`**: the vault's balance must increase, the depositor's balance must decrease by the same amount, the depositor's share count must increase, and the vault's outstanding-share count must decrease by exactly the same amount (shares flow from the vault's pseudo-account to the depositor). The implementation also handles the issuer-deposit edge case: when a deposit is made in the vault's own IOU currency by the currency issuer, no trust-line balance change occurs because the issuer's payments create funds rather than moving them, so the asset-delta check on the transaction sender is skipped.

- **`ttVAULT_WITHDRAW`**: symmetric to deposit — vault balance decreases, recipient increases by the same amount, depositor's shares decrease, vault's outstanding shares increase by the same amount. A destination field may redirect proceeds to a different account, so the code checks exactly one of `tx[sfAccount]` or `tx[sfDestination]` for the balance increase rather than assuming the sender is always the recipient. Issuer-withdrawal has a corresponding carve-out.

- **`ttVAULT_SET`**: a reconfiguration transaction that must not alter any asset or share balance; both the vault's asset fields and the pseudo-account's actual on-ledger balance are verified to be unchanged.

- **`ttVAULT_CLAWBACK`**: enforced only when the caller is the asset's issuer, or when the vault owner is force-burning outstanding shares against a vault that holds no assets. In both cases, the holder's share count must decrease and the vault's outstanding shares must increase by the same amount; if the vault held assets, the vault balance must also decrease by the asset delta.

Across all transaction types, three cross-cutting rules apply: `lossUnrealized` may only change during `ttLOAN_MANAGE` or `ttLOAN_PAY`; immutable fields (`sfAsset`, `sfAccount` pseudo-id, `sfShareMPTID`) must never change on an existing vault; and only transactions tagged with `mustModifyVault` or `mayModifyVault` privileges (via `hasPrivilege`) are permitted to touch vault state at all. These privilege checks act as a firewall against misconfigured or malicious transaction types somehow reaching vault objects.

## Relationship to the Broader Invariant Framework

`ValidVault` sits at position 24 in the `InvariantChecks` tuple. Its enforcement scope is deliberately narrower than the general-purpose checkers (like `XRPNotCreated` or `AccountRootsNotDeleted`) that run unconditionally. `ValidVault` gates all meaningful work behind an `afterVault_.empty() && beforeVault_.empty()` early exit, making it essentially free for the overwhelming majority of transactions that never touch a vault. The use of `Number` (a high-precision rational type from `xrpl/basics/Number.h`) rather than raw integers for all asset and loss fields reflects the fact that vault assets can be denominated in IOU currencies with fractional precision requirements that 64-bit integers cannot represent losslessly.