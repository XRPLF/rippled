# `MPTokenIssuanceSet.h` — MPT Issuance Lifecycle Management Transactor

## Role in the System

`MPTokenIssuanceSet` is the transactor that handles `ttMPTOKEN_ISSUANCE_SET` transactions on the XRP Ledger. Its responsibility is twofold: it can either toggle the lock/unlock state on an existing MPTokenIssuance ledger object or on an individual holder's `MPToken` slot, and — when the `featureDynamicMPT` amendment is active — it can mutate post-creation properties of an issuance such as its `mutableFlags`, `sfTransferFee`, `sfMPTokenMetadata`, and `sfDomainID`.

The class fits into the broader MPT (Multi-Purpose Token) transactor family, which also includes `MPTokenIssuanceCreate`, `MPTokenIssuanceDestroy`, and `MPTokenAuthorize`. Together these classes manage the full lifecycle of token issuances on the ledger.

## Inheritance and the Static-Polymorphism Pattern

`MPTokenIssuanceSet` inherits from the abstract base class `Transactor` and follows the framework's deliberate pattern of *static* (compile-time) polymorphism rather than virtual dispatch for the three main pipeline phases. The base class template `invokePreflight<T>` drives the entire preflight pipeline — it calls `T::checkExtraFeatures`, then `preflight1` (account/fee sanity), then `T::preflight`, then `preflight2` (signature validation). By declaring each of these methods as `static`, derived transactors participate in the pipeline without the overhead or accidental-override risk of virtual functions. `ConsequencesFactory{Normal}` tells the framework that this transaction follows standard fee-consequence rules — it does not unconditionally block future transactions from the same account.

## The Four Static Entry Points

**`checkExtraFeatures`** guards amendment requirements that aren't already captured by the global transaction-to-feature map. For `MPTokenIssuanceSet`, the only extra requirement is that a transaction carrying `sfDomainID` must have both `featurePermissionedDomains` *and* `featureSingleAssetVault` enabled simultaneously. Returning `false` causes `invokePreflight` to return `temDISABLED` without ever touching the rest of the pipeline.

**`getFlagsMask`** returns `tfMPTokenIssuanceSetMask`, restricting the flags field to only bits this transaction legitimately owns. `preflight1` uses this mask to reject any transaction that sets undefined flag bits, keeping the flags space tidy and extensible.

**`preflight`** performs stateless semantic validation. Several invariants are checked here: `sfDomainID` and `sfHolder` are mutually exclusive; `tfMPTLock` and `tfMPTUnlock` cannot be set simultaneously; the submitting account cannot be the same as the `sfHolder`. Under `featureDynamicMPT`, additional rules apply: mutation fields (`sfMutableFlags`, `sfMPTokenMetadata`, `sfTransferFee`) cannot coexist with `sfHolder` or non-universal tx flags; you cannot simultaneously set a non-zero `sfTransferFee` and clear `tmfMPTClearCanTransfer`; and the `sfMutableFlags` value must not be zero and must not include unknown bits. Importantly, when either `featureSingleAssetVault` or `featureDynamicMPT` is enabled, a transaction that changes *nothing* (zero flags, no domain, no mutation fields) is rejected as `temMALFORMED` — an empty mutation is meaningless.

**`checkPermission`** handles delegate authorization. When the transaction carries an `sfDelegate` field, it looks up the delegate SLE and first tries a broad transaction-level permission check via `checkTxPermission`. If that fails, it falls back to granular permissions: `MPTokenIssuanceLock` is required for `tfMPTLock` and `MPTokenIssuanceUnlock` for `tfMPTUnlock`. This layered approach lets a delegate be granted either blanket `MPTokenIssuanceSet` authority or fine-grained lock-only or unlock-only authority.

## Preclaim: State-Dependent Validation

`preclaim` runs after signature verification against a read-only ledger view. It verifies that the target `MPTokenIssuance` object exists and that the submitter is the issuer. The logic around `lsfMPTCanLock` deserves attention: if the issuance does not have `lsfMPTCanLock` set and neither `featureSingleAssetVault` nor `featureDynamicMPT` is enabled, any lock/unlock attempt fails with `tecNO_PERMISSION`. Under those newer amendments the lock flag is only required if the tx actually tries to lock or unlock. This backwards-compatible branching avoids breaking existing behaviour for older ledger states.

When `sfHolder` is present, `preclaim` additionally verifies that the holder account exists and that their `MPToken` slot exists for this issuance. For `sfDomainID`, it enforces that the issuance has `lsfMPTRequireAuth` set (only auth-required issuances use domain-restricted holder sets), and that the referenced `PermissionedDomain` object exists (unless the zero value is supplied to clear the domain link).

Mutation validation in `preclaim` uses a table-driven approach. The file-local `mptMutabilityFlags` array (defined in the `.cpp`) maps each set/clear tx flag pair to the corresponding `canMutateFlag` that must be present in the current `sfMutableFlags` of the ledger object. `std::any_of` over this array checks that every flag the transaction tries to change was granted as mutable at issuance time. `sfMPTokenMetadata` and `sfTransferFee` have their own dedicated mutable-flag checks (`lsmfMPTCanMutateMetadata` and `lsmfMPTCanMutateTransferFee`). One subtle rule: setting a *non-zero* `sfTransferFee` requires `lsfMPTCanTransfer` to already be set on the issuance — setting `tmfMPTSetCanTransfer` in the same transaction does not satisfy this requirement.

## Applying the Transaction

`doApply` performs the actual ledger mutation. It peeks at either the issuance SLE or the holder's `MPToken` SLE depending on whether `sfHolder` was provided. Flag transitions for `lsfMPTLocked` are straightforward OR/AND-NOT operations. Mutable-flag changes iterate over the `mptMutabilityFlags` table, setting or clearing each `canMutateFlag` as directed.

A notable invariant: when `tmfMPTClearCanTransfer` is applied, `doApply` explicitly calls `sle->makeFieldAbsent(sfTransferFee)` to keep the ledger internally consistent — you cannot have a non-zero transfer fee on an issuance that no longer permits transfers. Similarly, when `sfTransferFee` is set to zero, the field is removed rather than stored as zero, because the field uses `soeDEFAULT` semantics where absent means zero. The same absent/empty idiom applies to `sfMPTokenMetadata`: an empty blob removes the field entirely. For `sfDomainID`, the zero sentinel value (`beast::zero`) removes the domain link, while any other value sets it.

After all mutations are applied, `view().update(sle)` commits the change. The method always returns `tesSUCCESS` — all failure modes are caught earlier in the pipeline, so `doApply` reaching a failure path is treated as an internal consistency error and returns `tecINTERNAL`.