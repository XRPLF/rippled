# `src/libxrpl/protocol/Permissions.cpp`

## Role and Context

This file implements the `Permission` class, a read-only singleton that is the central authority for XRPL's account-delegation permission system. When one account grants another the right to submit transactions on its behalf (via `DelegateSet`), every permitted action is represented as a numeric `permissionValue` stored on-ledger. `Permission` owns the authoritative mapping between those numbers, their human-readable names, the underlying transaction types they correspond to, and the amendments that must be active before they can be delegated.

The class is the single place where the permission number-space is defined and validated, so it is consulted by transaction preflight logic (directly in `DelegateSet::preflight()`) and by any code that needs to translate between the serialized ledger representation and meaningful protocol semantics.

## Two Classes of Permission and Their Numeric Encoding

The XRPL permission system has two orthogonal kinds of delegation grants:

**Transaction-level permissions** allow the delegate to submit any transaction of a given type on the grantor's behalf. These are encoded as `TxType + 1`. Since `TxType` values are zero-based 16-bit integers (e.g. `ttPAYMENT = 0`), the offset of +1 shifts them to start at 1 and keeps them entirely within the `uint16` range. The static helpers `txToPermissionType()` and `permissionToTxType()` encapsulate this arithmetic.

**Granular permissions** are sub-operation grants within a single transaction type. For example, `TrustlineAuthorize`, `TrustlineFreeze`, and `TrustlineUnfreeze` are three distinct granular permissions that all correspond to `ttTRUST_SET`, giving a grantor the ability to delegate individual TrustSet capabilities rather than the whole transaction type. These are defined in `permissions.macro` with values starting at 65537 (just above `UINT16_MAX`), placing them entirely outside the uint16 transaction-permission range. The constructor's per-entry assert — `permission.second > UINT16_MAX` — enforces this partition at program startup, making namespace collisions a hard failure rather than a silent bug.

## Macro-Driven Table Construction

The constructor is the most architecturally interesting part of the file. It populates five `unordered_map` members by expanding the same two macro files (`transactions.macro` and `permissions.macro`) with different `#define TRANSACTION` / `#define PERMISSION` definitions each time. This X-macro technique means all permission metadata — values, names, transaction type associations, amendment requirements, and delegability flags — live exclusively in the macro files. Adding a new transaction type or granular permission requires editing only those two files; `Permissions.cpp` and the `GranularPermissionType` enum update automatically.

Each expansion is wrapped in `#pragma push_macro` / `#pragma pop_macro` to prevent the temporary macro definition from persisting or interfering with surrounding code — a defensive pattern that matters here because `transactions.macro` is included in many other translation units with different definitions.

The five maps are:

- **`txFeatureMap_`** — maps `TxType` (stored as `uint16_t`) to the `uint256` amendment that must be enabled before this transaction type can be used in delegation. A zero `uint256{}` means the transaction requires no specific amendment.
- **`delegableTx_`** — maps `TxType` to `Delegation::delegable` or `Delegation::notDelegable`. Governance and key-management transactions are typically marked `notDelegable` because allowing an agent to change signers or disable the master key would undermine the account owner's control.
- **`granularPermissionMap_`** — maps string names (e.g. `"TrustlineAuthorize"`) to `GranularPermissionType` values, for deserializing permission names from JSON.
- **`granularNameMap_`** — the inverse of the above, for serializing permission values to human-readable output.
- **`granularTxTypeMap_`** — maps `GranularPermissionType` to its parent `TxType`, which is needed so the amendment guard in `isDelegable()` can locate the feature requirement for any granular sub-operation.

A final constructor assert verifies that `txFeatureMap_` and `delegableTx_` have the same number of entries, catching any hypothetical macro inconsistency where a transaction appears in one but not the other.

## `isDelegable()`: The Delegation Gate

This method is called from `DelegateSet::preflight()` for every entry in the `sfPermissions` array before a delegation is accepted. Its logic distinguishes granular from transaction-level permissions:

For a **granular permission**, the check is simply whether the value resolves to a known `GranularPermissionType`. If it does, delegation is unconditionally allowed. The rationale is that granular permissions are already intentionally narrow by design.

For a **transaction-level permission**, delegation is allowed only if all three of these hold: the decoded `TxType` is recognized in `delegableTx_`, the transaction's required amendment is currently active in `rules` (or no amendment is required), and the `Delegation` flag for that transaction type is `delegable`. The amendment check is significant: it prevents a transaction from being delegated before the ledger feature that introduces it is live, even if the macro data already includes the new transaction type.

## Singleton and Thread Safety

`getInstance()` returns a function-local `static const Permission` instance, which is initialized once and never mutated. C++11 and later guarantee that this initialization is thread-safe, making the pattern appropriate for a read-only registry that is consulted from multiple threads during transaction processing. Copy construction and copy assignment are explicitly deleted, enforcing the singleton contract.

## Relationship to `TxFormats`

`getPermissionName()` delegates to `TxFormats::getInstance().findByType()` for the transaction-level case. This keeps the string names of transaction types canonical — they are defined once in `TxFormats` and reused here rather than being duplicated in the permissions system.