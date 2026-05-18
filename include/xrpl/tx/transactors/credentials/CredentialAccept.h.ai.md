# `CredentialAccept.h` — Subject-Side Credential Acceptance Transactor

## Role and Context

`CredentialAccept` is one of three credential transactors in the XRPL credential subsystem, alongside `CredentialCreate` and `CredentialDelete`. It implements the second step of the credential issuance lifecycle: after an issuer creates a credential directed at a subject account (`CredentialCreate`), the subject must explicitly accept it before the credential becomes active and usable in permission-gated operations.

The header declares the class interface only; all logic lives in the corresponding `.cpp`. The three-stage transactor pipeline (`preflight` → `preclaim` → `doApply`) is mandated by the base `Transactor` class through compile-time polymorphism — the framework calls these stages via `invokePreflight<T>` and the `preclaim`/`doApply` dispatch chain rather than virtual dispatch, which is why the static methods use name-hiding rather than `override`.

## Class Design

`CredentialAccept` inherits `Transactor` directly with no additional member data. The `ConsequencesFactory{Normal}` constant signals to the fee/consequence framework that this transaction follows standard consequence semantics: it consumes a sequence number and has no special blocking or custom consequence logic.

The constructor simply forwards `ApplyContext` to the base, consistent with all three credential transactors. The uniformity here is intentional — the three credential types have identical structural signatures, differing only in their validation logic and ledger mutations.

## Processing Stages

**`getFlagsMask`** participates in flag validation during preflight. It returns `tfUniversalMask` when the `fixInvalidTxFlags` amendment is active, restricting the transaction to only universal flags and rejecting any unknown flags as malformed. When the amendment is absent (pre-fix ledgers), it returns `0`, which the framework interprets as "allow any flags" — preserving backward compatibility with transactions submitted before the flag-stricter rules were enabled.

**`preflight`** performs structural validation against the transaction fields themselves, without touching ledger state. It enforces two invariants: the `sfIssuer` field must not be the zero account ID (which would indicate a malformed or defaulted field), and `sfCredentialType` must be non-empty and within the maximum credential type length. Both checks return `temINVALID_ACCOUNT_ID` or `temMALFORMED` respectively — `tem` errors signal permanent rejection that no future ledger state can cure, so the transaction will not be retried.

**`preclaim`** performs stateful read-only checks against the current ledger view. It verifies that the issuer account actually exists (returning `tecNO_ISSUER` otherwise), that the credential object keyed by `(subject, issuer, credentialType)` exists in the ledger (`tecNO_ENTRY`), and that the credential has not already had its `lsfAccepted` flag set (`tecDUPLICATE`). These `tec` results still consume a fee but do not alter ledger state, appropriate for situations where on-chain state has changed between submission and processing.

**`doApply`** executes the actual state transition under an `ApplyView` that buffers writes until success. Several design choices stand out:

- **Reserve check at apply time**: Rather than checking the reserve in `preclaim`, the implementation defers it to `doApply` and compares `preFeeBalance_` (balance before fees, captured by the base class) against the post-increment owner reserve. This ensures the fee has already been deducted before the reserve calculation, reflecting actual affordability.

- **Owner count ownership transfer**: When a credential is created by an issuer but not yet accepted, it is counted against the issuer's owner count (they "own" the pending credential object). On acceptance, `doApply` calls `adjustOwnerCount` twice: decrementing the issuer's count by 1 and incrementing the subject's count by 1. The reserve burden thus shifts atomically from issuer to subject at the moment of acceptance, which incentivizes subjects to promptly manage credentials they hold.

- **Expiry handled at apply time**: If the credential has an expiration and that expiration has passed by `parentCloseTime`, `doApply` deletes the credential via `credentials::deleteSLE` and returns `tecEXPIRED`. This is an important cleanup mechanism: expired credentials are removed from ledger state even when the accepting transaction itself fails. The code carefully checks whether the deletion succeeded before returning `tecEXPIRED` versus propagating the deletion error.

- **Accepted state flip**: On the happy path, the method simply sets `sfFlags` to `lsfAccepted` on the credential SLE and calls `view().update()`. There is no new object creation — the credential already exists from the `CredentialCreate` step; acceptance is a mutation of that existing object.

## Error Handling and Defensive Patterns

The `tefINTERNAL` guard at the top of `doApply` (marked `LCOV_EXCL_LINE`) protects against the theoretical case where `sleSubject` or `sleIssuer` cannot be loaded even though their existence was confirmed in `preclaim`. This path is unreachable under correct ledger invariants — the credential's existence implies both accounts exist — but the guard keeps the code safe against hypothetical corruption and documents the assumption explicitly.

## Relationship to Siblings

All three credential transactors (`CredentialCreate`, `CredentialCreate`, `CredentialDelete`) share an identical header shape: same constructor pattern, same static method signatures, `ConsequencesFactory{Normal}`. This structural uniformity is not coincidental — it means the transactor dispatch machinery can treat them identically at the framework level, with all semantic differences confined to the `.cpp` implementations.