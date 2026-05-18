# CredentialAccept.cpp

## Role in the System

`CredentialAccept` implements the `CredentialAccept` transaction for the XRPL's W3C-aligned Verifiable Credentials feature. It sits at the end of the two-step credential issuance handshake: a credential begins its life when an issuer submits `CredentialCreate`, which places the credential object in both the issuer's and subject's owner directories but counts the reserve cost against the issuer alone. The subject must then submit a `CredentialAccept` transaction to signal consent, at which point ownership — and the associated XRP reserve burden — transfers from issuer to subject. Until that acceptance, the credential exists in the ledger but carries the `lsfAccepted` flag cleared, making it invisible to any authorization checks downstream.

## Transaction Phases

Like every XRPL transactor, the logic is split across three static/virtual hooks executed sequentially by the engine.

**`getFlagsMask`** gates which transaction flags are meaningful. When the `fixInvalidTxFlags` amendment is active, it returns `tfUniversalMask`, causing the framework to reject any unknown flags set by the sender. Before the amendment, returning `0` suppressed this check entirely — a legacy behavior preserved for replay compatibility.

**`preflight`** performs pure field-level validation with no ledger access. It checks that `sfIssuer` is non-zero (an all-zero account ID is never a valid address) and that `sfCredentialType` is both non-empty and no longer than `maxCredentialTypeLength`. These two invariants are sufficient to reject obviously malformed transactions early, before any state reads are attempted.

**`preclaim`** performs read-only ledger validation. It verifies that the issuer's account root actually exists (`tecNO_ISSUER`), that a credential object keyed on `(subject, issuer, credentialType)` is present in the ledger (`tecNO_ENTRY`), and that the credential has not already been accepted (`tecDUPLICATE`). The order matters: checking the credential's existence before checking the flag avoids a null-dereference on the SLE pointer.

**`doApply`** is where the state mutations occur.

## doApply: Design Decisions

**Reserve check deferred to doApply.** The reserve is checked against `preFeeBalance_` — the subject's balance snapshot taken *before* the transaction fee is deducted — rather than checking in `preclaim`. Doing so in `preclaim` would use the pre-fee balance too, but only `doApply` has access to `preFeeBalance_` as a member of the `Transactor` base class. The check requires the owner count incremented by one to compute the required reserve for the incoming credential object.

**Expiry check with active cleanup.** Even though `preclaim` validates that the credential exists, its expiry is not checked there. Instead, `checkExpired` is called in `doApply` against `view().header().parentCloseTime`. The reason: the XRPL design philosophy intentionally delays cleanup of expired objects until a transaction tries to use them. When an expiry is detected here, `credentials::deleteSLE` removes the credential from the ledger and its owner directories entirely, even though the transaction itself returns `tecEXPIRED`. This is a non-trivial detail: `tec`-class errors in XRPL *do* commit state changes (the fee is charged and any side effects — including this deletion — are persisted), which means the ledger cleans itself up opportunistically at the cost of an acceptance attempt.

**Ownership transfer via `adjustOwnerCount`.** The core semantic work is two lines:

```cpp
adjustOwnerCount(view(), sleIssuer, -1, j_);
adjustOwnerCount(view(), sleSubject, 1, j_);
```

`CredentialCreate` placed the credential in both directories but only increased the issuer's owner count. `CredentialAccept` completes the transfer by decrementing the issuer's count and incrementing the subject's, shifting the reserve obligation. The credential's directory membership itself is unchanged — both entries were written at creation time.

**Invariant guard for `tefINTERNAL`.** After `preclaim` establishes that the credential exists — which implicitly means both account roots exist — `doApply` still re-fetches both account SLEs with `view().peek()` and returns `tefINTERNAL` if either is missing. This is marked `LCOV_EXCL_LINE` because the code path should be unreachable in correct operation: it guards against theoretical inconsistency in the ledger state that preclaim cannot prevent between validation and application phases.

## Relationship to Sibling Transactors

`CredentialCreate` establishes the ledger object and increments the issuer's reserve. `CredentialAccept` (this file) acknowledges it and transfers the reserve. `CredentialDelete` can be submitted by either party to remove the credential entirely, releasing the reserve back to whoever currently holds ownership. Together they form a complete lifecycle that maps to the W3C Verifiable Credentials model: issue → accept → optionally revoke.

The `credentials::deleteSLE` helper called on expiry in `doApply` is the same routine used by `CredentialDelete`, ensuring consistent directory cleanup regardless of which removal path is taken.