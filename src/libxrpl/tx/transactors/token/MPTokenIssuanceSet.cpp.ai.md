# `MPTokenIssuanceSet.cpp` — MPToken Issuance State Mutation Transactor

This file implements the `MPTokenIssuanceSet` transaction type for the XRPL's Multi-Purpose Token (MPT) system. The transaction serves two distinct but related purposes: it allows an issuer to lock or unlock an MPToken issuance (or an individual holder's MPToken), and — under the `featureDynamicMPT` amendment — to mutate fields on an existing `MPTokenIssuance` ledger object that were originally designed to be set at creation time: behavioral flags, metadata, and the transfer fee.

## Class Structure

`MPTokenIssuanceSet` inherits from `Transactor` and follows the standard XRPL transactor pipeline. The class exposes the standard static hooks (`checkExtraFeatures`, `getFlagsMask`, `preflight`, `checkPermission`, `preclaim`) plus the virtual `doApply()`. All methods except `doApply` are static because they operate on context-carrying argument objects rather than `this`; `doApply` is the only method that needs mutable ledger access through the inherited `view()`.

## The Two-Level Flag System

A critical design pattern running throughout the file is the distinction between two classes of flags on an `MPTokenIssuance` ledger object:

- **Operational flags** (`lsfMPT*`): describe the current state of the issuance — whether it is locked, whether it requires authorization, whether clawback is enabled, etc.
- **Mutability flags** (`lsmfMPTCanMutate*`): stored in the `sfMutableFlags` field, these act as meta-flags that declare _which_ operational flags and fields the issuer is allowed to change post-creation.

This two-level design enforces post-issuance governance promises to holders: if an issuer creates an MPToken issuance without `lsmfMPTCanMutateCanTransfer`, the transfer policy is locked at creation. The `MPTokenIssuanceSet` transaction can never change it, regardless of what the issuer subsequently submits.

## The `mptMutabilityFlags` Table

The file-local `MPTMutabilityFlags` struct and the `mptMutabilityFlags` constexpr array are the architectural backbone for mutation handling:

```cpp
static constexpr std::array<MPTMutabilityFlags, 6> mptMutabilityFlags = {
    {{tmfMPTSetCanLock, tmfMPTClearCanLock, lsmfMPTCanMutateCanLock},
     ...}};
```

Each entry maps the set-bit and clear-bit in the transaction's `sfMutableFlags` field to the corresponding mutability gate on the issuance SLE. This table-driven approach is used in three places: `preflight` checks for set-and-clear-same-flag conflicts, `preclaim` checks whether the issuer has permission to change each flag, and `doApply` iterates the table to apply changes. The design ensures the three phases always reason about the same six properties with no possibility of a mismatch between which flags are validated and which are applied.

## `preflight` — Stateless Validation

`preflight` has two modes depending on which fields are present. If `sfMutableFlags`, `sfMPTokenMetadata`, or `sfTransferFee` is present (`isMutate`), the transaction is in mutation mode and `featureDynamicMPT` must be enabled. This gates the entire new mutation feature behind an amendment, preserving backward compatibility.

The `sfDomainID` and `sfHolder` mutual exclusion reflects the semantics of the two operations: domain assignment targets the issuance object itself, while `sfHolder` targets an individual holder's `MPToken` object — they cannot both be present in a single transaction.

The no-op check (under `featureSingleAssetVault` or `featureDynamicMPT`) rejects transactions that neither set any flags nor include any mutation fields, preventing fee-burning no-op submissions.

When in mutation mode, the validator enforces two important interaction rules: you cannot name a `sfHolder` (because mutation operates on the issuance, not on a holder's token), and you cannot set transaction-level flags alongside mutation fields (they would apply to different objects). The prohibition on setting a non-zero `sfTransferFee` while simultaneously clearing `tmfMPTClearCanTransfer` prevents a contradictory single-transaction state where a fee is set on an issuance whose transfer capability is then immediately removed.

## `checkPermission` — Granular Delegation

This static method supports the XRPL delegate feature. If the transaction has no `sfDelegate` field, the issuer signed directly and is unconditionally permitted. When a delegate is present, the method first tries a broad per-transaction-type permission check via `checkTxPermission`. If that fails, it falls back to examining granular permissions: `MPTokenIssuanceLock` and `MPTokenIssuanceUnlock` are checked individually against the lock/unlock flags. The dead-code comment (`// LCOV_EXCL_LINE`) on the broad-mask guard is honest self-documentation — currently no other `MPTokenIssuanceSet`-specific transaction flags exist, so the branch cannot be reached, but it is retained as defensive forward-compatibility infrastructure.

## `preclaim` — Ledger State Checks

`preclaim` performs checks that require reading ledger state. The `lsfMPTCanLock` check has deliberate asymmetric logic: if either `featureSingleAssetVault` or `featureDynamicMPT` is enabled and the transaction is not actually locking or unlocking, the absence of `lsfMPTCanLock` is not fatal. This allows mutation operations (metadata, fees, flags) on issuances that were not created with locking capability, without accidentally blocking non-locking mutations just because the issuance lacks lock permission.

The transfer fee interaction is the subtlest preclaim check. A non-zero `sfTransferFee` requires `lsfMPTCanTransfer` to be already set _on the ledger before this transaction_. Enabling `tmfMPTSetCanTransfer` in `sfMutableFlags` within the same transaction does not satisfy this requirement. The `preflight` layer already blocks the reverse case (non-zero fee while clearing transfer), but `preclaim` is needed here because it's the first point where the current ledger state of `lsfMPTCanTransfer` is visible.

The domain assignment check requires `lsfMPTRequireAuth` on the issuance — binding a permissioned domain to an issuance that doesn't already use authorization would be meaningless.

## `doApply` — Ledger Mutation

`doApply` begins by selecting the correct SLE: if `sfHolder` is present it peeks the holder's `MPToken` object, otherwise it peeks the `MPTokenIssuance`. Lock/unlock operations flip `lsfMPTLocked` directly on whatever SLE was selected, which is how both issuance-wide and per-holder locking are handled by the same transaction.

For mutation operations, the method iterates `mptMutabilityFlags` setting or clearing `lsmfMPTCanMutate*` flags in `sfFlags`. Clearing `tmfMPTClearCanTransfer` also removes the `sfTransferFee` field atomically — you cannot have a fee-bearing issuance with transfer disabled.

Both `sfTransferFee` and `sfMPTokenMetadata` use "absent means default" semantics: a zero fee or empty metadata string removes the field entirely rather than storing a zero/empty value. For `sfDomainID`, `beast::zero` serves as a sentinel to clear an existing domain, mirroring how similar sentinel-clear patterns appear elsewhere in the XRPL protocol.