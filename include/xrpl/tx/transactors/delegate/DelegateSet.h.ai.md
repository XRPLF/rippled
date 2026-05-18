# `DelegateSet.h` — Transaction Transactor for Account Permission Delegation

`DelegateSet.h` declares the `DelegateSet` transactor, which implements the XRPL transaction type that allows an account owner to grant or revoke a set of named permissions to a delegate account. It sits in the `xrpl::tx::transactors::delegate` module alongside `DelegateUtils`, and follows the same three-phase pipeline contract (`preflight` → `preclaim` → `doApply`) mandated by the `Transactor` base class.

## Purpose and Role

The delegate feature enables one account to authorize another to submit specific transaction types on its behalf — a form of scoped delegation without relinquishing ownership. `DelegateSet` manages the lifecycle of the on-ledger `Delegate` SLE (Serialized Ledger Entry) that records this relationship: creating it, updating its permission array, or deleting it when the caller submits an empty permissions array. A single transaction type therefore serves three operations, distinguished entirely by whether the `sfPermissions` array is non-empty and whether an existing delegate object exists.

## Transactor Lifecycle

`DelegateSet` follows the standard static-dispatch preflight pattern. `ConsequencesFactory{Normal}` is declared at class scope, indicating that this transaction type claims a fee normally (not a blocker that should suppress other transactions).

**`preflight`** runs without any ledger state and enforces structural validity. It rejects transactions with more permissions than `permissionMaxSize`, prevents self-authorization (`sfAccount == sfAuthorize`), detects duplicate permission values in the array using an `unordered_set`, and validates each permission value against `Permission::getInstance().isDelegable()` to ensure only delegable permission types are included. Because it has no ledger access, this phase is safe to run during initial transaction reception before any consensus context is established.

**`preclaim`** runs against a read-only ledger view and checks for existence conditions that can only be verified with ledger state: both the grantor account and the `sfAuthorize` target account must exist. A specific guard rejects a delete-intent transaction (empty permissions array) when no delegate object exists yet — preventing a fee-charging no-op.

**`doApply`** is the only virtual method and the sole ledger-mutating phase. It branches across three distinct paths based on what's present in the ledger:

- **Update path**: If a `Delegate` SLE already exists and the permissions array is non-empty, the existing `sfPermissions` field is replaced in place. No reserve change occurs because the owner-count entry already exists.
- **Delete path**: If a `Delegate` SLE already exists and the permissions array is empty, execution delegates to the static `deleteDelegate()` method.
- **Create path**: If no SLE exists, the account's pre-fee XRP balance is checked against the reserve required for one additional owned object. If sufficient, a new `Delegate` SLE is constructed with both account IDs and the permission array, inserted into the owner directory, and `adjustOwnerCount` is called to increment the owner's object count. The `sfOwnerNode` field on the new SLE records the owner directory page index, which is later used for O(1) directory removal without scanning.

## The `deleteDelegate` Static Interface

The most architecturally notable element is `deleteDelegate()`. The comment `// Interface used by AccountDelete` signals an explicit cross-transactor contract: `AccountDelete.cpp` calls `DelegateSet::deleteDelegate` directly to remove delegate objects owned by an account being deleted. Examining the implementation confirms this — `AccountDelete.cpp` line 169 routes its delegate cleanup through this exact function.

Making deletion a public static method rather than keeping it private to `doApply` reflects a deliberate design choice: the deletion logic requires an `ApplyView`, an SLE pointer, an account ID, and a journal, all of which are available to any transactor operating in the apply phase. By exposing it statically, `AccountDelete` can reuse the cleanup code without needing a fake `DelegateSet` transaction object. This avoids code duplication while keeping deletion logic co-located with the type that owns the `Delegate` SLE's structure.

Internally, `deleteDelegate()` performs three steps: remove the SLE's entry from the owner directory using the stored `sfOwnerNode`, decrement the owner count via `adjustOwnerCount`, and erase the SLE from the ledger. A failure in `dirRemove` logs at `fatal` severity and returns `tefBAD_LEDGER`, indicating ledger state corruption rather than a recoverable user error — this branch is marked `LCOV_EXCL` because reaching it would represent an impossible-in-practice ledger inconsistency.

## Invariants and Defensive Guards

The `tecINTERNAL` return at the "no existing SLE, empty permissions" branch of `doApply` is a defensive invariant. The `preclaim` phase already blocks this case with `tecNO_ENTRY`, so `doApply` should never reach that branch. The guard exists because `doApply` does not trust the upstream contract absolutely — it defends against any hypothetical bypass of the preclaim check.

Similarly, the `tefINTERNAL` guard when `sleOwner` cannot be found at apply time is marked `LCOV_EXCL_LINE`, confirming it is considered unreachable given correct ledger state (the account's existence was verified in `preclaim`).