# `CredentialHelpers.cpp` — Credential Lifecycle and Authorization Helpers

This file implements the complete set of helper functions that govern credential lifecycle management and authorization checking in the XRPL ledger. Credentials on the XRP Ledger are ledger objects (SLEs) linking an issuer to a subject, optionally gated by expiration and requiring subject acceptance. This module is the shared logic layer consumed by payment, escrow, payment-channel, vault, and MPToken transactors whenever those transactions need to verify that a sender is authorized via credentials or deposit pre-authorization.

## The `ReadView`/`ApplyView` Split and the Two-Phase Validation Pattern

The single most important architectural decision in this file is the deliberate pairing of read-only `preclaim` functions with writable `doApply` counterparts. Ledger mutations — specifically deleting expired credential objects — are only legal during `doApply`, when the transactor holds an `ApplyView`. But authorization rejection must be decided in `preclaim` so the transaction can be rejected before fee collection. Expired credentials complicate this: a transaction may arrive at preclaim with credentials that have just expired, and those objects need to be cleaned up even if the transaction itself is eventually rejected.

The design resolves this tension with an explicit two-phase protocol:

- `credentials::validDomain()` (takes `ReadView const&`) scans the domain's accepted credential list, identifies any that are expired, and returns `tecEXPIRED` if all valid credentials are expired. The caller in preclaim is expected to suppress that specific error and allow the transaction to reach `doApply`.
- `verifyValidDomain()` (takes `ApplyView&`) re-runs the same check but this time calls `credentials::removeExpired()` to physically delete the expired SLEs. It then re-checks whether any live, accepted credential remains.

This same pattern applies to deposit preauthorization: `credentials::valid()` checks credential existence and ownership in preclaim using a `ReadView`, while `verifyDepositPreauth()` handles the mutable side in doApply — calling `credentials::removeExpired()` on the credential IDs in the transaction before checking whether the destination account's `DepositPreauth` entry allows the sender.

The effect is that any transaction touching credentials acts as a passive garbage collector for expired credential objects, even transactions that ultimately fail.

## Credential Deletion: `deleteSLE()` and Dual Owner Directories

Deleting a credential SLE is non-trivial because a credential is indexed in *two* owner directories: the issuer's and the subject's. The SLE stores `sfIssuerNode` and `sfSubjectNode` field offsets so it can be found in each directory without a search. The inner `delSLE` lambda encapsulates the per-account removal: it peeks the account SLE, calls `view.dirRemove()` to remove the credential from that account's directory, and conditionally adjusts the owner reserve count.

The reserve accounting follows the credential lifecycle: before the subject accepts (`lsfAccepted` is unset), only the issuer holds the reserve burden; after acceptance, the subject takes ownership and the burden shifts. This is implemented in `deleteSLE()` by passing `isOwner = !accepted || (subject == issuer)` to the issuer-side call, and `isOwner = accepted` to the subject-side call. When issuer and subject are the same account, only one directory removal is performed.

Internal invariant failures (missing account SLE, `dirRemove` returning false) are wrapped in `LCOV_EXCL_START`/`STOP` blocks — these paths indicate ledger corruption that is believed to be unreachable under normal operation, so they are intentionally excluded from coverage analysis.

## Expiration: `checkExpired()` and the `uint32_t::max` Sentinel

`checkExpired()` reads `sfExpiration` from the credential SLE using the optional-field accessor (`~sfExpiration`), defaulting to `std::numeric_limits<uint32_t>::max()` when the field is absent. Comparing `now > max` is always false, meaning credentials without an expiration field never expire without any special-case branching. The time source is `view.header().parentCloseTime`, ensuring deterministic behavior across all validators — the *parent* ledger's close time rather than the current wall clock.

`removeExpired()` iterates a `STVector256` of credential keys, peeks each SLE, and deletes any that are expired. The comment noting that credentials were already validated in preclaim explains why this loop only checks expiration: existence and ownership were already confirmed; here only the time gate matters. Expired credentials are deleted even if the outer transaction fails (`doApply` sets them up to be written regardless of the main transaction result because removal happens unconditionally before the main result is returned).

## Transaction Field Validation: `checkFields()` and `checkArray()`

Two sibling functions handle syntactic validation at preflight time, covering the two ways credentials appear in transactions.

`checkFields()` validates the `sfCredentialIDs` field (a `STVector256` of 256-bit hashes referencing existing credential objects). It enforces non-empty, bounded size (at most `maxCredentialsArraySize = 8`), and uniqueness via an `unordered_set<uint256>`.

`checkArray()` validates credential arrays that appear in `DepositPreauth` and `PermissionedDomainSet` transactions, where credentials are specified as `(issuer, credentialType)` pairs rather than object hashes. It validates issuer account ID validity, `credentialType` length (1 to `maxCredentialTypeLength = 64` bytes), and duplicate detection using `sha512Half(issuer, credentialType)` — hashing the pair to catch logical duplicates even when the binary representations differ subtly.

## Deposit Preauth Credential Authorization: `authorizedDepositPreauth()`

This function implements the credential-based path of deposit preauthorization. It builds a `std::set<std::pair<AccountID, Slice>>` of `(issuer, credentialType)` pairs from the submitted credential IDs, then checks whether `keylet::depositPreauth(dst, sorted)` exists in the ledger. The sorted set representation matches how `DepositPreauth` objects are keyed, allowing an O(log n) lookup against the single ledger entry.

The `lifeExtender` vector is a deliberate lifetime management artifact. `Slice` is a non-owning view into the SLE's underlying storage. If the `shared_ptr<SLE const>` were allowed to drop reference count to zero before the lookup, the `Slice` in the sorted set would dangle. Keeping all SLE pointers alive in `lifeExtender` for the duration of the function prevents this.

`makeSorted()` is the companion utility that builds the same sorted structure from an `STArray`, used at `DepositPreauth` creation time to normalize the set before generating the keylet.

## Relationship to Transactors

`verifyDepositPreauth()` is called from `Payment`, `EscrowFinish`, and `PaymentChannelClaim` transactors during `doApply` when the destination account has the `lsfDepositAuth` flag set. It short-circuits for self-payments (`src == dst`), then falls through to account-level preauth check (`keylet::depositPreauth(dst, src)`) before falling back to credential-based preauth via `authorizedDepositPreauth()`. `verifyValidDomain()` is called by `MPTokenHelpers` and vault transactors during `doApply` to enforce domain-based credential gating on MPToken issuances and vaults.