# `CredentialDelete.cpp` — Credential Deletion Transactor

## Role in the System

`CredentialDelete` implements the transaction handler for removing on-ledger verifiable credential objects from the XRP Ledger. Credentials, modeled after the W3C Verifiable Credentials specification, are issued by one account (`sfIssuer`) and addressed to a subject account (`sfSubject`). Once created they persist in ledger state indefinitely unless explicitly deleted or, in some flows, cleaned up as a side-effect of other transactions. This transactor provides the explicit deletion path.

Like all transactors in the XRPL codebase, `CredentialDelete` follows the three-phase lifecycle: `preflight` for stateless format checks, `preclaim` for read-only ledger validation, and `doApply` for the actual state mutation.

## Transaction Field Semantics

A notable design choice is that both `sfSubject` and `sfIssuer` are optional in the transaction, but at least one must be present. When either field is absent, `preclaim` and `doApply` silently substitute the transaction's signing account (`account_`) via `.value_or(account)`. This means:

- An issuer deleting their own credential only needs to provide `sfSubject`.
- A subject deleting a credential they hold only needs to provide `sfIssuer`.
- The issuer and subject can be the same account (self-issued credential), in which case neither field is technically necessary beyond the check that at least one must appear.

The `preflight` rejects zeroed `AccountID` values for either field, and validates `sfCredentialType` is non-empty and within `maxCredentialTypeLength`. These are formatting invariants the ledger enforces uniformly across all credential transactions.

## Permission Model in `doApply`

The most architecturally significant logic lives in the permission check in `doApply`:

```cpp
if ((subject != account_) && (issuer != account_) &&
    !checkExpired(sleCred, ctx_.view().header().parentCloseTime))
{
    return tecNO_PERMISSION;
}
```

This enforces three principals who may delete a credential:
1. **The subject** (credential holder) — may delete at any time.
2. **The issuer** — may delete at any time (issuers can revoke credentials they've created).
3. **Any third party** — may only delete if the credential has already expired.

The third-party path exists for ledger hygiene. Once a credential's expiration has passed it becomes inert but continues to consume reserve space. Allowing anyone to sweep expired credentials incentivizes garbage collection without compromising active credential integrity. Expiry is checked against `parentCloseTime` — the close time of the *parent* ledger rather than the current one — which is a consensus-deterministic value available at apply time without ambiguity.

## Existence Checks Across Phases

`preclaim` verifies the credential exists using `ctx.view.exists(keylet::credential(...))`, returning `tecNO_ENTRY` if not found. `doApply` then re-fetches the SLE via `view().peek()`. If `peek` returns null after `preclaim` confirmed existence, the code returns `tefINTERNAL` (marked `LCOV_EXCL_LINE` — unreachable in practice). This double-check is defensive: `tef` errors signal ledger corruption rather than user error, and they abort transaction application rather than charging a fee, which distinguishes them semantically from `tec` errors.

## Actual Deletion via `deleteSLE`

`doApply` delegates the low-level removal to `credentials::deleteSLE()` defined in `CredentialHelpers.cpp`. That function handles the non-trivial aspects of credential lifecycle:

- A credential is tracked in *two* owner directories: one for the issuer (`sfIssuerNode`) and, once accepted, one for the subject (`sfSubjectNode`).
- Owner counts are maintained per-account for reserve calculation. Before acceptance (`lsfAccepted` flag unset), the reserve is charged to the issuer; after acceptance it transfers to the subject. `deleteSLE` inspects the `lsfAccepted` flag to correctly decrement the right account's owner count.
- Both directory entries and the SLE itself are removed, releasing the reserve held by whichever account owned the object.

## `getFlagsMask` and Amendment Gating

`getFlagsMask` returns `tfUniversalMask` when the `fixInvalidTxFlags` amendment is active, or `0` (allow any flags) when it is not. The value `0` is a sentinel in the XRPL transactor framework meaning "accept all flag bits without complaint," preserving backward compatibility for transactions submitted before the amendment enabled stricter flag enforcement.

## Relationship to Sibling Transactors

`CredentialCreate` creates the SLE, inserts it into the issuer's owner directory, and optionally the subject's directory. `CredentialAccept` transfers ownership from issuer to subject by setting `lsfAccepted`. `CredentialDelete` is the terminal operation, and its permission model mirrors the lifecycle: either party may close out the relationship, and the ledger will also accept deletion from any party once the credential's utility has expired.