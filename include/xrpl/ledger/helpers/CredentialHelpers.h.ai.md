# `include/xrpl/ledger/helpers/CredentialHelpers.h`

This header is the central contract for credential and deposit pre-authorization logic throughout the XRPL ledger. It lives alongside a family of per-feature helpers (`AMMHelpers.h`, `EscrowHelpers.h`, etc.) and is included by every fund-transfer transactor — `Payment`, `EscrowFinish`, `PaymentChannelClaim`, `VaultDeposit` — wherever those transactions must honor destination account access controls.

## The Two-Phase Design Constraint

The most important architectural fact about this file is that its functions divide cleanly along the `preclaim` / `doApply` transaction-processing boundary, and this division is non-negotiable.

During `preclaim`, the ledger view is read-only (`ReadView`). Validators may execute preclaim in parallel, so no mutations are allowed. Expiration checks can happen here — you can read an `sfExpiration` field — but you cannot delete the expired SLE from the ledger.

During `doApply`, the view is mutable (`ApplyView`). This is the only phase where expired credential objects can actually be removed, owner directories adjusted, and owner counts decremented.

The header is explicit about this split in its inline comments. `credentials::valid()` checks existence, subject ownership, and the `lsfAccepted` flag, but deliberately defers expiration: "Expiration checks are in doApply." If you call `valid()` in preclaim and it passes, you must also call `verifyDepositPreauth()` in doApply to handle the deletion of any credential that has since expired. The same pattern applies to the domain pathway: `credentials::validDomain()` returns `tecEXPIRED` if it finds an expired credential but cannot remove it; `verifyValidDomain()` (outer namespace, takes `ApplyView`) is the doApply counterpart that actually prunes them.

This split is why the two "verify" functions live in the outer `xrpl::` namespace while the lower-level checks live in `xrpl::credentials::`. The naming convention signals which layer operates on mutable state.

## Credential Lifecycle: Validation and Deletion

`checkExpired()` is the primitive: it reads the optional `sfExpiration` field from a credential SLE (defaulting to `uint32_t::max` if absent) and compares against the ledger's `parentCloseTime`. This is used by both `removeExpired()` and `validDomain()`.

`deleteSLE()` is the most intricate function. A credential SLE belongs to two owner directories — one for the issuer, one for the subject — and the reserve-count ownership model depends on whether the credential has been accepted:

- If the credential is **not yet accepted** (subject hasn't acknowledged it), only the issuer holds the reserve. Deleting decrements the issuer's owner count but not the subject's.
- If **accepted** and issuer ≠ subject, ownership transfers to the subject; the subject's count is decremented on deletion.
- If issuer == subject (self-issued credentials), the accepted flag is irrelevant for the issuer-side delete — the owner count adjustment uses `!accepted || (subject == issuer)` to capture both cases.

The function removes the SLE from both owner directories (using `dirRemove`) and then erases it from the ledger. Internal errors that would indicate a corrupted ledger state are marked `LCOV_EXCL` because they are unreachable in a correctly functioning node.

`removeExpired()` iterates a `STVector256` of credential IDs, peeks each via `ApplyView`, and calls `deleteSLE()` for any that are expired. It returns `true` if any were found, which the callers use to short-circuit with `tecEXPIRED` — deleting the credentials as a side effect even when the enclosing transaction ultimately fails.

## Input Validation: Two Different Credential Array Shapes

Two separate validation functions exist because credentials appear in two structurally different forms across the protocol:

`checkFields()` validates `sfCredentialIDs` — a `STVector256` of opaque 256-bit hashes that directly reference existing credential SLEs. This is the form used in fund-transfer transactions (Payment, etc.). It enforces non-empty, bounded size, and uniqueness via an `unordered_set<uint256>`.

`checkArray()` validates the `STArray` form used in `DepositPreauth` and `PermissionedDomainSet` transactions, where credentials are declared as `(issuer, credentialType)` pairs rather than pre-computed hashes. Duplicate detection here hashes each pair with `sha512Half(issuer, credentialType)` to produce a canonical key — the same digest the ledger uses to derive the object key — ensuring logical duplicates are caught even if the raw bytes differ.

## Authorization Check: DepositPreauth with Credentials

`verifyDepositPreauth()` implements the full gate for deposit-authorization enforcement. It handles two pre-authorization modes:

1. **Account-level**: a simple `keylet::depositPreauth(dst, src)` lookup — the destination has explicitly preauthorized the source account.
2. **Credential-based**: if account-level authorization fails and the transaction carries `sfCredentialIDs`, it delegates to `authorizedDepositPreauth()`, which checks whether the destination has preauthorized the specific credential set the source is presenting.

The credential-based path in `authorizedDepositPreauth()` reconstructs a sorted `std::set<std::pair<AccountID, Slice>>` from the credential SLEs. This sorted set is the canonical form used as the key for a `DepositPreauth` ledger object that was created against a credential specification. A subtle memory-safety detail: `Slice` is a non-owning pointer into the SLE's internal buffer, so the function maintains a `lifeExtender` vector of `shared_ptr<SLE const>` alongside the set to keep those SLEs alive for the duration of the `view.exists()` lookup.

## Domain-Credential Pathway

`validDomain()` (preclaim-safe) and `verifyValidDomain()` (doApply) cover the `PermissionedDomain` access model, which differs from `DepositPreauth`. Here the domain object itself carries an `sfAcceptedCredentials` array of `(issuer, credentialType)` specs. The check asks: does the subject hold an accepted, non-expired credential matching any entry in that array?

`validDomain()` returns `tecEXPIRED` rather than `tecNO_AUTH` if at least one matching credential was found but expired — a signal to the caller that the condition might resolve (via deletion) in doApply. `verifyValidDomain()` then collects all matching credential keys, runs `removeExpired()` on them, and re-reads the survivors to confirm one is accepted.

`makeSorted()` is the utility behind credential array deduplication in transaction setup: given a credential array, it builds the same sorted-pair set used by the authorization lookups, returning an empty set if any duplicates are encountered.