# `CredentialCreate.h` — CredentialCreate Transactor

## Role in the System

`CredentialCreate` is the transactor responsible for processing `CredentialCreate` transactions on the XRP Ledger, which implements W3C Verifiable Credentials (VC). The feature allows a trusted issuer to attest facts about a subject account — their identity, compliance status, or any other verifiable attribute — in a way that can be inspected by third parties without contacting the issuer again. This header declares the class interface; the implementation lives in the corresponding `.cpp`.

Like every transaction type in rippled, `CredentialCreate` inherits from `Transactor` and participates in the three-phase pipeline: **preflight** (stateless validation), **preclaim** (read-only ledger checks), and **doApply** (mutating ledger state).

## Class Design

`CredentialCreate` follows the standard transactor pattern exactly. `ConsequencesFactory` is set to `Normal`, meaning the transaction claims a fee under ordinary fee-burning rules rather than acting as a queue blocker or using custom consequence computation.

The constructor simply forwards `ApplyContext` to the base class — no additional state is needed because all inputs come from the transaction fields stored in `ctx_`.

The four static/virtual members form the complete contract:

- `getFlagsMask` — returns the permitted bitfield of transaction flags. When the `fixInvalidTxFlags` amendment is active it returns `tfUniversalMask`, restricting callers to only the universal flags. When the amendment is absent it returns `0`, which the framework interprets as "allow any flags" — a backward-compatibility escape hatch for transactions submitted before the fix was deployed.

- `preflight(PreflightContext const&)` — runs before any ledger state is consulted, so it is cheap and can be cached across retries. It rejects transactions where `sfSubject` is absent, where the optional `sfURI` is present but empty or exceeds `maxCredentialURILength`, and where `sfCredentialType` is empty or exceeds `maxCredentialTypeLength`. All three return `temMALFORMED`, which prevents a fee from being claimed.

- `preclaim(PreclaimContext const&)` — executes against a read-only view of the current ledger. It confirms that the target subject account actually exists (returning `tecNO_TARGET` otherwise) and that the (`subject`, `issuer`, `credentialType`) triple does not already identify an existing credential (`tecDUPLICATE`). These checks are separated from `preflight` because they require ledger lookups that must not be cached across ledger closes.

- `doApply()` — the only virtual override, where the actual ledger mutation happens.

## What `doApply` Does

The implementation constructs a new `SLE` (serialized ledger entry) keyed by `keylet::credential(subject, account_, credType)`. Several invariants are enforced before the object is inserted:

**Expiration validation** is deferred to `doApply` rather than `preflight` because whether a timestamp is in the past depends on the ledger's `parentCloseTime`, which is not available during preflight. If the optional `sfExpiration` field is present and already behind the ledger's parent close time, the transaction fails with `tecEXPIRED`. Notably, expiration uses the *parent* close time, not the current ledger's own close time — this is consistent with XRPL's convention for time-sensitive operations.

**Reserve check** happens after the expiration guard. The issuer's current owner count is read from their `AccountRoot`, one is added prospectively, and the resulting reserve is compared against `preFeeBalance_` (the issuer's balance before fees were deducted). Failing this test returns `tecINSUFFICIENT_RESERVE`.

**Directory insertion** follows the two-party structure of the VC model. The credential is always inserted into the *issuer's* owner directory, and the issuer's owner count is incremented by one — the issuer bears the reserve cost for the credential until the subject accepts it. If the subject and issuer are the same account (self-issuance), the credential is immediately marked `lsfAccepted` and inserted only into the single shared directory. For a third-party credential the object is also inserted into the *subject's* owner directory (without incrementing the subject's owner count), providing subject-side discoverability and enabling `CredentialAccept` to later transfer economic ownership. The `sfIssuerNode` and `sfSubjectNode` fields record the directory page indices needed for efficient deletion.

## Relationship to Sibling Transactors

`CredentialAccept.h` and `CredentialDelete.h` are structurally identical headers, each following the same `getFlagsMask`/`preflight`/`preclaim`/`doApply` pattern. Together the three transactors form the full lifecycle of a credential: `CredentialCreate` issues it, `CredentialAccept` lets the subject accept it (transferring the reserve obligation), and `CredentialDelete` removes it from both directories. The design makes the state transitions explicit on-ledger and independently auditable, which is essential for a compliance-oriented primitive.

## Non-Obvious Design Decisions

The choice to split validation across `preflight` and `preclaim` rather than consolidating it in `doApply` is deliberate: preflight results can be memoized by the transaction queue and re-used across ledger closes, whereas preclaim is re-executed each time the ledger state changes. Keeping expensive ledger lookups out of preflight improves throughput under load.

Self-issuance being auto-accepted removes the need for a holder to issue a follow-up `CredentialAccept` transaction just to use credentials they issued to themselves — a practical simplification for single-entity deployments that issue and consume credentials on the same account.