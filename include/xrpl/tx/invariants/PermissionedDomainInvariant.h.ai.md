# `PermissionedDomainInvariant.h` — Invariant Guard for Permissioned Domain Objects

## Role in the System

This header declares `ValidPermissionedDomain`, one of the invariant checkers registered in the `InvariantChecks` tuple in `InvariantCheck.h`. The XRPL invariant-checking framework runs every registered checker after every transaction application, regardless of whether the transaction succeeded or failed. If any checker returns `false`, the ledger mutation is rolled back and the node hard-fails to avoid propagating corrupt state.

`ValidPermissionedDomain` enforces the structural integrity of `ltPERMISSIONED_DOMAIN` ledger entries, specifically the `sfAcceptedCredentials` array that defines which credential types are accepted by a domain. These structural guarantees are foundational: downstream logic that iterates the credentials array (for example, `credentials::validDomain`) depends on them to be non-empty, de-duplicated, and lexicographically sorted without re-validating on every access.

## The Two-Phase Visitor Pattern

Like all XRPL invariant checkers, `ValidPermissionedDomain` follows a two-phase pattern declared in `InvariantChecker_PROTOTYPE`:

**Phase 1 — `visitEntry`:** Called once per modified ledger entry during the transaction's journal replay. For any SLE that is not of type `ltPERMISSIONED_DOMAIN`, the method returns immediately. For matching entries it records an `SleStatus` snapshot of the *post-modification* (`after`) state: how many credentials exist, whether they are free of duplicates (via `credentials::makeSorted`, which returns empty on duplicates), whether the existing order matches the canonical sorted order, and whether this is a deletion. All snapshots accumulate in `sleStatus_`, a `std::vector<SleStatus>`.

**Phase 2 — `finalize`:** Called once after all entries are visited. It interprets the collected `SleStatus` observations in the context of the full transaction (`STTx`) and its result code (`TER`). Only here does the invariant actually pass or fail.

## The `SleStatus` Inner Struct

The private `SleStatus` struct is a compact result bundle for a single ledger-entry observation:

- `credentialsSize_` — raw credential count, used to detect empty (0) or oversized (> `maxPermissionedDomainCredentialsArraySize`, which is 10) arrays.
- `isSorted_` — true only when the iteration order of `sfAcceptedCredentials` matches the order produced by `credentials::makeSorted`; false immediately if any element is out of place.
- `isUnique_` — true when `makeSorted` returned a non-empty set (it returns empty to signal duplicates).
- `isDelete_` — propagates the `isDel` flag from `visitEntry` so `finalize` can distinguish a create/update from a deletion.

The design choice to pre-compute `isSorted_` and `isUnique_` in `visitEntry` rather than in `finalize` keeps the final validation logic simple and avoids re-reading the SLE through `ReadView` a second time. The sorted-check short-circuits on the first out-of-order pair, making it O(n) rather than a full re-sort.

## `finalize` Behavior and the Feature-Flag Split

The most architecturally significant aspect of `finalize` is its branch on `view.rules().enabled(fixPermissionedDomainInvariant)`.

**Without the amendment (legacy path):** The check is narrow — it only fires for a successful `ttPERMISSIONED_DOMAIN_SET` that touched at least one domain entry, validating the four credential constraints. Any other transaction type, or any failed transaction, is silently passed (`return true`). This matches the original, conservative scope.

**With the amendment (strict path):** The invariant becomes substantially more comprehensive:
- A failed transaction must not have mutated any domain entries at all (`sleStatus_.empty()` on failure).
- No transaction may ever modify more than one domain entry in a single application.
- A `ttPERMISSIONED_DOMAIN_SET` must create or modify (not delete) exactly one domain entry, and that entry must satisfy all four credential constraints.
- A `ttPERMISSIONED_DOMAIN_DELETE` must delete exactly one domain entry and must not modify it.
- Any other transaction type that touches a domain entry is flagged as unauthorized.

This bifurcation follows the standard XRPL amendment pattern: the amendment locks in the stricter invariant network-wide once a supermajority of validators enable it, while nodes running pre-amendment rules still validate correctly on pre-amendment ledgers. The test suite (`Invariants_test.cpp`) exercises both branches explicitly by toggling `fixPermissionedDomainInvariant` in the feature set.

## Relationship to Adjacent Code

`credentials::makeSorted` (from `CredentialHelpers.h`) is the shared utility that both the transactor (`PermissionedDomainSet`) and this invariant rely on to define canonical sort order. The transactor calls `credentials::checkArray` during `preclaim`/`doApply` to reject malformed submissions early; the invariant re-checks the persisted state as a last-resort defense, ensuring that no code path — including a future bug in the transactor — can write an invalid domain object into a finalized ledger.

The constant `maxPermissionedDomainCredentialsArraySize = 10` is defined in `Protocol.h` and shared across the transactor, the invariant, and the test suite, making it the single source of truth for the capacity limit.