# `DelegateSet.cpp` — DelegateSet Transaction Transactor

## Role in the System

`DelegateSet.cpp` implements the `DelegateSet` transaction type, which allows an XRPL account to authorize a second account to submit certain transactions on its behalf. The resulting on-ledger object — a `Delegate` — records the granting account (`sfAccount`), the authorized account (`sfAuthorize`), and the set of `sfPermissions` that have been delegated. The transaction can create a new delegation, overwrite an existing one with a different permission set, or delete the delegation entirely by submitting an empty permissions array.

This file is one file in a small cluster under `src/libxrpl/tx/transactors/delegate/`: `DelegateUtils.cpp` provides the runtime check helpers (`checkTxPermission`, `loadGranularPermission`) that use the stored `Delegate` SLE at execution time, while `DelegateSet.cpp` is solely concerned with lifecycle management of that SLE.

## Validation Pipeline

The transactor follows the standard three-phase XRPL pattern: stateless preflight, stateful preclaim, and ledger-mutating apply.

### `preflight` — stateless checks

Four invariants are enforced before the ledger state is consulted:

1. **Array size bound**: the `sfPermissions` array must not exceed `permissionMaxSize` (10, defined in `Protocol.h`). Exceeding it returns `temARRAY_TOO_LARGE`.
2. **No self-delegation**: `sfAccount == sfAuthorize` is rejected with `temMALFORMED`. An account cannot grant itself a permission it already inherently has.
3. **No duplicate permissions**: an `std::unordered_set<uint32_t>` accumulates each `sfPermissionValue` as the array is iterated; a failed `insert` (meaning the value was already present) returns `temMALFORMED`.
4. **Delegability check**: each permission value is validated against `Permission::getInstance().isDelegable(value, rules)`. The singleton `Permission` registry knows, per-value and per-enabled-amendments, whether a transaction type or granular permission is legally delegable. Non-delegable values (e.g., transactions that manipulate signing authority themselves) return `temMALFORMED`.

The `Permission` singleton distinguishes two namespaces of permission values: standard transaction-type permissions (where `permissionValue == TxType + 1`, fitting in 16 bits) and granular permissions (values above `UINT16_MAX`, enumerated in `permissions.macro`). This encoding is why the uniqueness check works cleanly on raw `uint32_t` — both namespaces fit in the same integer space without collision.

### `preclaim` — stateful checks

Two existence checks run against the read-only ledger view:

- The submitting account must exist (`terNO_ACCOUNT` if not — marked `LCOV_EXCL_LINE` because the transactor framework normally guarantees this).
- The target account (`sfAuthorize`) must exist (`tecNO_TARGET`). A delegation to a non-existent account is meaningless and could create dangling state.

A third check handles the delete path: if the permissions array is empty (signaling delete intent) but no `Delegate` object for `(account, authorizeAccount)` exists, `tecNO_ENTRY` is returned. This prevents a no-op delete from succeeding silently and consuming fees unexpectedly.

## `doApply` — Ledger Mutation

The apply logic branches on whether the delegate SLE already exists:

**Update path**: if the SLE exists, the permissions array in it is atomically replaced with the new one from the transaction. If the new array is empty, `deleteDelegate` is called instead.

**Delete path** (via `deleteDelegate`): the SLE is removed from the owner's directory (`dirRemove`), the owner count is decremented by one via `adjustOwnerCount`, and the SLE itself is erased from the view. `deleteDelegate` is a `static` method exposed in the header specifically so `AccountDelete` can call it when cleaning up all objects owned by an account being deleted.

**Create path**: a reserve check ensures the account holds enough XRP to absorb the new owner-count increment (`preFeeBalance_ < reserve` → `tecINSUFFICIENT_RESERVE`). The new SLE is keyed by `keylet::delegate(account, authorizedAccount)` — a composite key that makes lookups for a specific delegation O(1). The SLE is inserted into the owner directory (`dirInsert`), and `sfOwnerNode` records the directory page for efficient future removal. A `tecDIR_FULL` guard covers the case where the directory has no room (marked `LCOV_EXCL_LINE` as it is extremely rare in practice).

A notable defensive check appears at the start of the create branch: if the SLE does not exist yet but `permissions` is empty, the code returns `tecINTERNAL`. In theory this state is unreachable because `preclaim` already rejects it with `tecNO_ENTRY`, but the guard is retained as a belt-and-suspenders invariant for the apply phase.

## Resource and Ownership Model

Each `Delegate` SLE is owned by the granting account: it appears in that account's owner directory and increments its `sfOwnerCount`. The owner-count-based reserve requirement (`accountReserve(ownerCount + 1)`) ensures the ledger cannot be cheaply filled with delegation objects. When the delegation is deleted — either explicitly via `DelegateSet` or implicitly via `AccountDelete` — the count is decremented and the reserve is released.

The `deleteDelegate` static interface is the primary seam for `AccountDelete` integration. The `AccountDelete` transactor calls it passing the `ApplyView`, the SLE pointer, and the account ID, without needing to know anything about how the directory or count management works.

## Error-Code Taxonomy

The file is careful to use the right TER class for each failure:
- `tem*` codes from `preflight` indicate malformed transactions that are rejected before any ledger state is read.
- `ter*` codes from `preclaim` indicate transient or existence failures — the transaction might succeed if retried later.
- `tec*` codes from `doApply` are consensus-applied failures that consume the fee.
- `tef*`/`tefINTERNAL` marks logic faults that should never occur in a correctly operating node (all `LCOV_EXCL_LINE` annotated, confirming they are untested and treated as unreachable by design).