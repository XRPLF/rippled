# `PermissionedDomainInvariant.cpp`

## Role in the System

This file implements `ValidPermissionedDomain`, one of the post-transaction invariant checks that the XRPL applies to every transaction before finalizing its effects on the ledger. The invariant specifically guards `ltPERMISSIONED_DOMAIN` ledger entries, which represent on-ledger access-control constructs whose validity depends entirely on the structural correctness of their `sfAcceptedCredentials` array. If that array is empty, oversized, contains duplicates, or is out of canonical order, downstream authorization logic would either fail silently or produce non-deterministic results across validator nodes — hence the need for a hard post-apply check.

## Two-Phase Invariant Pattern

Like all invariant checks in the XRPL engine, `ValidPermissionedDomain` follows the framework's two-phase contract. During transaction application, `ApplyContext` calls `visitEntry` once per modified ledger entry. After the transaction has been fully applied, `ApplyContext` calls `finalize` with the completed transaction and its result code. The class accumulates state between these calls in `sleStatus_`, a `std::vector<SleStatus>` with one element per modified `ltPERMISSIONED_DOMAIN` entry.

## `visitEntry`: Observation and Pre-Analysis

`visitEntry` filters immediately on ledger entry type — entries that aren't `ltPERMISSIONED_DOMAIN` are silently ignored, keeping the check narrowly scoped. The method only examines the `after` state, not `before`, because the invariant's goal is to ensure the resulting ledger is valid; the pre-transaction state is immaterial for this purpose.

For each qualifying entry the inner `check` lambda calls `credentials::makeSorted` on the raw `sfAcceptedCredentials` array. The behavior of `makeSorted` is central to how both uniqueness and sort order are verified simultaneously: it attempts to insert each `(sfIssuer, sfCredentialType)` pair into a `std::set`, and if any insertion fails due to an existing equal element it immediately returns an empty set. This means the `isUnique_` flag in `SleStatus` is set from `!sorted.empty()` — a non-obvious convention where "empty result" signals "duplicates found."

Sort order is then checked only when `isUnique_` is true (duplicates invalidate the sorted comparison anyway). The code walks both the canonical `std::set` and the original `STArray` in lockstep via an index counter, comparing `sfIssuer` and `sfCredentialType` at each position. This avoids re-sorting the full credential objects; it reuses the already-computed `makeSorted` output.

The resulting `SleStatus` records four values: the raw credential count, whether the array is sorted, whether it is unique, and whether the entry is being deleted (`isDel`).

## `finalize`: Conditional Validation Under Feature Flag

`finalize` contains the most architecturally interesting logic: a hard branch on `view.rules().enabled(fixPermissionedDomainInvariant)` that selects between an older, narrower invariant and a newer, comprehensive one. This pattern is common in XRPL for amendments that tighten protocol rules without retroactively altering historical ledger state.

**Pre-amendment path** (feature not enabled): The check only runs when the transaction type is `ttPERMISSIONED_DOMAIN_SET` and the result was successful. This was the original invariant scope, verifying the credential array is valid after a set operation. It ignores delete transactions entirely.

**Post-amendment path** (feature enabled, `fixPermissionedDomainInvariant`): This is significantly stricter. First, if the transaction failed (`!isTesSuccess(result)`), `sleStatus_` must be empty — a failed transaction must not have touched any permissioned domain entry at all. Second, a single transaction may affect at most one domain entry; if `sleStatus_.size() > 1` the invariant fails. Third, the check branches on transaction type:

- `ttPERMISSIONED_DOMAIN_SET`: must have affected exactly one entry, that entry must not be a deletion, and the credentials must pass all four structural checks (non-empty, ≤ `maxPermissionedDomainCredentialsArraySize` which is 10, unique, sorted).
- `ttPERMISSIONED_DOMAIN_DELETE`: must have affected exactly one entry, and the entry must be flagged as deleted. No credential checks are needed for a deletion.
- Any other transaction type: must not have affected any domain entries at all. An unexpected modification by an unrelated transaction type is an invariant violation.

The inner `check` lambda in `finalize` independently re-validates the same structural properties (non-empty, within size limit, unique, sorted) using the `SleStatus` data captured during `visitEntry`. The redundancy is intentional: `visitEntry` captures facts about each entry independently, while `finalize` applies the policy that ties those facts to the transaction type and its outcome.

## Error Handling

All invariant failures are reported via `JLOG(j.fatal())` and return `false`. A `false` return from any invariant's `finalize` causes `ApplyContext` to roll back the transaction entirely, protecting the ledger from entering an invalid state. There is no exception path — the invariant contract is enforced through return values, consistent with the broader XRPL error-handling philosophy.

## Relationship to Sibling Files

`PermissionedDomainInvariant.h` defines the `ValidPermissionedDomain` class with its private `SleStatus` struct, keeping the public interface to just two methods. `credentials::makeSorted` in `CredentialHelpers.cpp` is the shared utility that both this invariant and the `PermissionedDomainSet` transactor rely on for canonical credential ordering, ensuring the same normalization contract is applied both at write time and at invariant-check time. The `maxPermissionedDomainCredentialsArraySize = 10` constant from `Protocol.h` is the shared cap enforced both by `preflight` validation in the transactor and again here as a post-application safety net.