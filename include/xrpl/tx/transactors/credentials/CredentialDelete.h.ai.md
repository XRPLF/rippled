# `CredentialDelete.h` — Transaction Transactor for Credential Deletion

## Role in the System

`CredentialDelete.h` declares the `CredentialDelete` transactor, one of three credential-lifecycle transaction handlers in XRPL (alongside `CredentialCreate` and `CredentialAccept`). Its purpose is to remove a `Credential` ledger object — the on-chain record that an issuer has asserted a verifiable claim about a subject — from the ledger state. The file is intentionally minimal: a single class declaration that defers all logic to its `.cpp` counterpart.

## Class Design and Inheritance

`CredentialDelete` inherits from `Transactor`, the base class for all XRPL transaction processors. The `Transactor` framework mandates a three-phase execution model:

1. **`preflight`** — stateless, runs before ledger access, rejects clearly malformed transactions early.
2. **`preclaim`** — read-only ledger access, checks whether the transaction is likely to succeed given current state.
3. **`doApply`** — mutates ledger state; only reached after both prior phases pass.

The `static` qualifier on `preflight`, `preclaim`, and `getFlagsMask` is a deliberate design in the `Transactor` framework. These are called through `invokePreflight<T>()` via C++ name-hiding rather than virtual dispatch, enabling compile-time polymorphism without vtable overhead. The base `Transactor` provides default no-op implementations; derived classes override by shadowing the names. This is documented explicitly in `Transactor.h` with the warning *"these are not really virtual and so don't have the compiler-time protection that comes with it."*

The `ConsequencesFactory{Normal}` constant signals to the transaction queuing system that this transaction carries normal fee semantics — it does not block subsequent transactions from the same account in the queue (unlike a `Blocker`-typed transactor).

## Validation Logic (from the Implementation)

`getFlagsMask()` participates in the `fixInvalidTxFlags` amendment rollout. Before that amendment activated, it returned `0`, which the framework interprets as "accept any flags." After activation it returns `tfUniversalMask`, enforcing the standard constraint that no unknown flag bits may be set. This pattern appears across most credential transactors as a forward-compatible flag gating mechanism.

`preflight()` enforces two structural invariants. First, at least one of `sfSubject` or `sfIssuer` must be present — their absence together means the transaction cannot identify which credential to delete, so it is rejected as `temMALFORMED`. Second, if either field is present its value must not be the zero `AccountID`, which would indicate a corrupt or intentionally poisoned transaction. The `sfCredentialType` blob is also checked: it must be non-empty and within `maxCredentialTypeLength` bytes.

`preclaim()` performs the ledger existence check. When `sfSubject` or `sfIssuer` is absent, the transaction sender (`sfAccount`) is substituted as the default. This is the design for self-deletion: a subject can omit `sfIssuer` if the subject is themselves the sender, and similarly an issuer can omit `sfSubject`. The credential's keylet is derived from the triple `(subject, issuer, credentialType)` via `keylet::credential()`; if the object is absent the call returns `tecNO_ENTRY`.

## Authorization Enforced in `doApply()`

The most significant business rule lives in `doApply()`. A third party — an account that is neither the subject nor the issuer of the credential — is only permitted to delete the credential if it is expired, as determined by `checkExpired()` comparing `sfExpiration` against the ledger's `parentCloseTime`. The subject or issuer can always delete the credential unconditionally. This asymmetry is intentional: it allows orphaned expired credentials to be pruned by anyone (reclaiming reserve), while preventing unauthorized deletion of valid credentials.

If the authorization check fails, `doApply()` returns `tecNO_PERMISSION`. On success, it delegates the actual removal to `credentials::deleteSLE()` from `CredentialHelpers.h`, which handles unlinking the object from the ledger's owner directory and decrementing the owner's reserve count.

## Relationship to Sibling Files

`CredentialCreate.h` and `CredentialAccept.h` have structurally identical declarations — all three credential transactors expose the same four-method interface (`getFlagsMask`, `preflight`, `preclaim`, `doApply`). This uniformity is enforced by the `Transactor` contract rather than a shared credential base class, keeping the credential group cohesive without adding an extra layer of inheritance.