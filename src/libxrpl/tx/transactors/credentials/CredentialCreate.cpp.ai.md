# `CredentialCreate.cpp` — XRPL Verifiable Credential Issuance

## Purpose and Context

This file implements the `CredentialCreate` transaction, which allows an account (the issuer) to issue a W3C-style [Verifiable Credential](https://www.w3.org/TR/vc-data-model-2.0/) on the XRP Ledger. The credential binds a subject account, an issuer account, and an arbitrary `CredentialType` blob into an on-ledger object (`ltCREDENTIAL`) that third parties can query without contacting the issuer. Together with `CredentialAccept` and `CredentialDelete` (sibling files in the same directory), this forms a three-transaction lifecycle for managing credentials.

The transactor follows the standard XRPL three-phase pattern — `preflight`, `preclaim`, and `doApply` — with each phase having well-defined responsibilities and error semantics.

## Three-Phase Lifecycle

### `getFlagsMask` and `preflight`

`getFlagsMask` returns `tfUniversalMask` only when the `fixInvalidTxFlags` amendment is active; otherwise it returns 0 (allowing any flags). This gating prevents protocol inconsistencies on networks where the amendment has not yet deployed.

`preflight` is purely stateless — it never touches the ledger view — so its results can be cached and reused across retry batches. It validates three fields:

- **`sfSubject`** must be present (non-zero `AccountID`). A missing subject yields `temMALFORMED`.
- **`sfURI`** is optional, but if present it must not be empty and must not exceed `maxCredentialURILength` (256 bytes, from `Protocol.h`). An empty URI is explicitly rejected because it would be indistinguishable from a missing one at retrieval time.
- **`sfCredentialType`** is required, must not be empty, and must not exceed `maxCredentialTypeLength` (64 bytes). This field is part of the composite ledger key, so size bounds are enforced here rather than at insert time.

All `preflight` failures return `tem…` codes, which mark the transaction as permanently malformed; no fee is consumed.

### `preclaim`

`preclaim` performs two ledger-state checks that are cheap to evaluate without producing side effects:

1. The subject account must exist (`keylet::account(subject)`). Issuing a credential against a non-existent account would create a dangling reference, returned as `tecNO_TARGET`.
2. A credential with the same `(subject, issuer, credentialType)` composite key must not already exist on the ledger. The uniqueness check uses `keylet::credential(subject, ctx.tx[sfAccount], credType)` — note that the transaction's `sfAccount` field is the issuer. Duplicates return `tecDUPLICATE`.

The reason the reserve check does not appear here (but does in `doApply`) is subtle: reserve requirements can change between `preclaim` and `doApply` if a fee change amendment activates mid-stream. Moving it to `doApply` is the conservative, correct choice.

### `doApply`

`doApply` is where the ledger is actually mutated. Several non-obvious decisions are worth noting:

**Expiration is validated here, not in `preflight`.** Because `sfExpiration` is a ledger close-time offset and the definitive `parentCloseTime` is only available inside the apply view's header, comparing the expiration against `preflight`'s notional "now" would be unreliable. The comparison `closeTime > *optExp` uses the actual close time from the ledger header, returning `tecEXPIRED` if the credential would be born already-expired.

**Reserve is checked against `preFeeBalance_`**, the issuer's balance *before* the transaction fee was deducted. This matches the general XRPL convention that reserve enforcement uses the pre-fee balance so the fee itself cannot push an account below its reserve in an inconsistent way.

**Self-issuance takes a shortcut.** When `subject == account_` (the issuer is also the subject), the credential SLE has `lsfAccepted` set immediately and is inserted only into the issuer/subject's single owner directory. A second `CredentialAccept` transaction would be redundant for this case. This is an important UX shortcut for self-attested claims.

**Third-party issuance creates a two-directory entry.** When the issuer and subject are distinct, the credential is inserted into *both* the issuer's and the subject's owner directories, with `sfIssuerNode` and `sfSubjectNode` recording the respective page numbers for O(1) deletion later. However, only the issuer's `ownerCount` is incremented — the subject bears no reserve cost until they call `CredentialAccept`, which transfers ownership (increments subject's count, decrements issuer's). If the subject never accepts, the issuer remains responsible for the reserve and can call `CredentialDelete` to reclaim it. This ownership model is the central invariant connecting all three sibling transactors.

**`tefINTERNAL` guards** on `sleCred` creation failure and on a missing issuer SLE are marked `LCOV_EXCL_LINE`, indicating they represent impossible runtime states given correct ledger integrity — but are left in for defensive completeness.

## Relationships to Sibling Files

- `CredentialAccept.cpp` consumes the pending credential created here: it checks `lsfAccepted == 0`, verifies the credential isn't expired, then sets `lsfAccepted`, decrements the issuer's owner count, and increments the subject's — completing the reserve transfer.
- `CredentialDelete.cpp` handles teardown: it removes the SLE and cleans up both directory entries, decrementing only one owner count (whichever party currently holds ownership).
- `CredentialHelpers.h` / `CredentialHelpers.cpp` provides shared utilities like `checkExpired`, `deleteSLE`, and `valid` used across all three transactors — none are called from `CredentialCreate` itself since it produces a new object rather than consuming an existing one.

## Key Invariants

- The composite key `(subject, issuer, credentialType)` is globally unique on the ledger; `preclaim` enforces this.
- Before acceptance, the issuer holds the reserve burden. After `CredentialAccept`, the subject holds it.
- Self-issued credentials are born accepted; third-party credentials start unaccepted.
- An expired-at-creation credential is rejected at `doApply` rather than silently stored and immediately expired, preventing zombie entries that would waste storage and confuse downstream authorization checks.