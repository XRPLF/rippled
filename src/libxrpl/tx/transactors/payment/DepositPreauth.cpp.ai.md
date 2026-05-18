# `DepositPreauth.cpp` — Transactor for Deposit Pre-Authorization

## Purpose and Context

XRPL accounts can enable the *Deposit Authorization* flag, which causes the ledger to reject incoming payments from unrecognized senders. `DepositPreauth.cpp` implements the `DepositPreauth` transaction type, whose sole job is to manage the whitelist entries that let other accounts (or holders of specific credential types) bypass that gate. Without this transactor, there would be no way to build or dismantle the allow-list that Deposit Authorization depends on.

The file is one of two transactors in the `payment/` subdirectory (alongside `Payment.cpp`) and inherits from `Transactor`, following the standard three-stage XRPL processing pipeline: `preflight` → `preclaim` → `doApply`.

## Four Modes in One Transaction Type

A single `DepositPreauth` transaction can do exactly one of four things, selected by which field is present in the transaction:

| Field | Effect |
|---|---|
| `sfAuthorize` | Create a preauth entry granting a specific `AccountID` |
| `sfUnauthorize` | Remove a previously granted `AccountID` preauth entry |
| `sfAuthorizeCredentials` | Create a preauth entry for a credential-type set |
| `sfUnauthorizeCredentials` | Remove a credential-type set preauth entry |

The credentials-based path (`sfAuthorizeCredentials` / `sfUnauthorizeCredentials`) is an amendment-gated extension: `checkExtraFeatures()` returns `false` (causing the transaction to be rejected before `preflight` is even called) if either credentials field is present but the `featureCredentials` amendment is not enabled. This is the standard XRPL mechanism for safely introducing new transaction fields in a backward-compatible way.

## Validation Pipeline

**`preflight`** is a static, ledger-free check that validates structure and field semantics. Its first and most important check enforces the "exactly one of four" constraint: it counts how many of the four allowed fields are present and returns `temMALFORMED` if that count is not exactly one. This approach — converting boolean presence flags to integers and summing them — is compact and explicit.

For the account-based path, `preflight` further validates that the target `AccountID` is non-zero (`temINVALID_ACCOUNT_ID`) and that an account is not pre-authorizing itself (`temCANNOT_PREAUTH_SELF`). The self-authorization check only applies to `sfAuthorize`; `sfUnauthorize` does not need it because a self-authorizing entry could never have been created in the first place.

For the credentials path, `preflight` delegates to `credentials::checkArray()`, which validates array size against `maxCredentialsArraySize` and verifies the structure of each credential element.

**`preclaim`** runs against a read-only ledger snapshot and checks runtime state: whether target accounts exist (`tecNO_TARGET`, `tecNO_ISSUER`), whether an entry being created is a duplicate (`tecDUPLICATE`), and whether an entry being removed actually exists (`tecNO_ENTRY`). The credential-based duplicate check requires building a `std::set<std::pair<AccountID, Slice>>` sorted by `(issuer, credentialType)`, which serves double duty: it catches duplicates within the array itself (returning `tefINTERNAL` if `emplace` fails to insert, though that path is guarded as `LCOV_EXCL_LINE`) and produces the canonical ordering needed to derive the deterministic ledger key.

## Apply Logic and Reserve Accounting

`doApply()` handles all four branches. The authorization branches (both account and credential) share a common pattern: first, they check that the account's *pre-fee balance* (`preFeeBalance_`, captured before fee deduction) covers the reserve required for one additional owned object. Using the pre-fee balance here is deliberate — it allows an account to dip into its reserve to pay the transaction fee while still being able to create the preauth entry, matching the behavior of other object-creating transactions throughout the ledger.

After the reserve check, the new `SLE` is populated and inserted into the ledger. The entry is also added to the account's owner directory via `view().dirInsert(keylet::ownerDir(account_), ...)`, and `sfOwnerNode` is written back onto the `SLE` so that removal can locate the directory page in O(1) without a linear scan. `adjustOwnerCount` is called to increment the owner's reserve obligation.

For the credentials path, the raw `STArray` from the transaction is re-sorted into a canonical `std::set` by `credentials::makeSorted()`, then reconstructed as an `STArray` for storage on the ledger entry. This ensures the ledger object's `sfAuthorizeCredentials` array always has a deterministic order regardless of how the submitter ordered the elements, which is required for the keylet derivation to be consistent.

## `removeFromLedger` — Shared Removal Utility

The `removeFromLedger(ApplyView&, uint256 const&, beast::Journal)` static method is deliberately not scoped to a particular transaction branch. Both the `sfUnauthorize` path and the `sfUnauthorizeCredentials` path call it with a pre-computed ledger key. More importantly, it is also called by the `AccountDelete` transactor when cleaning up owned objects before deleting an account — that's why the comment notes "Existence already checked in preclaim and AccountDelete." The method removes the entry from the owner directory, decrements `sfOwnerCount`, and erases the `SLE`. Failure to find the entry logs at `warn` and returns `tecNO_ENTRY`; an inconsistent directory removal (which should never happen on a healthy ledger) logs at `fatal` and returns `tefBAD_LEDGER`.

## Design Observations

The separation of `removeFromLedger` as a public static method rather than an internal helper reflects a real dependency: `AccountDelete` must be able to clean up preauth objects without going through the full transaction pipeline. This kind of cross-transactor utility is a recognized pattern in the XRPL codebase for objects that can be deleted both explicitly and as part of account cleanup.

The credentials sorting strategy — building a sorted set at both `preclaim` time (for the keylet lookup) and `doApply` time (for the stored array) — trades a small amount of repeated work for correctness: the ledger entry's key is derived from the sorted representation, so any mismatch between what was checked in `preclaim` and what is stored in `doApply` would silently create a bad entry. By using `credentials::makeSorted()` consistently in both phases, the code avoids that class of bug.