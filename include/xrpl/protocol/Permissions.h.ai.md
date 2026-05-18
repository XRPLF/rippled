# `include/xrpl/protocol/Permissions.h`

## Purpose

This header is the central definition point for XRPL's permission delegation system, which underpins the `DelegateSet` transaction type. It enables an account owner to delegate specific transaction-signing authority to another account in a controlled, fine-grained way — without handing over full account control. The design problem it solves is distinguishing *which* capabilities are delegated: a coarse grant ("the delegate may submit any `TrustSet`") versus a surgical grant ("the delegate may only *freeze* trustlines, not authorize them").

---

## Two-Tier Permission Model

The system maintains a strict numeric partition between two kinds of permissions, both represented as `uint32_t` values stored in `sfPermissionValue` on-ledger:

**Transaction-level permissions** cover an entire transaction type. The encoding is `TxType + 1`, implemented by the static helpers `txToPermissionType()` and `permissionToTxType()`. The `+1` shift ensures zero is never a valid permission value. Because all `TxType` values fit within 16 bits (`uint16_t`), transaction-level permissions always fall in the range `[1, UINT16_MAX]`.

**Granular permissions** cover sub-operations within a transaction type. Their values always exceed `UINT16_MAX` — the minimum value is `65537`. This is not merely a convention: it is asserted in the `Permission` constructor using `XRPL_ASSERT` for every entry in `granularPermissionMap_`. The numeric gap between the two ranges allows disambiguation without a type tag in the stored value.

---

## `GranularPermissionType` Enum

The `GranularPermissionType` enum is generated entirely from `detail/permissions.macro` using the X-macro pattern:

```cpp
#define PERMISSION(type, txType, value) type = value,
#include <xrpl/protocol/detail/permissions.macro>
```

The macro file defines twelve granular permissions (as of this writing), grouped by parent transaction type. For example, `TrustlineAuthorize`, `TrustlineFreeze`, and `TrustlineUnfreeze` all map to `ttTRUST_SET`, while `AccountDomainSet`, `AccountEmailHashSet`, `AccountMessageKeySet`, `AccountTransferRateSet`, and `AccountTickSizeSet` all map to `ttACCOUNT_SET`. This is significant because `AccountSet` itself is marked `notDelegable` at the transaction level — you cannot delegate broad `AccountSet` authority — but specific account-property mutations are permitted as granular grants.

The X-macro pattern is used twice: here in the header to generate the enum, and again five times in `Permissions.cpp` to build runtime lookup tables. Because the same `.macro` file drives all instantiations, adding a new granular permission requires only a single new `PERMISSION(...)` line.

---

## `Delegation` Enum

```cpp
enum Delegation { delegable, notDelegable };
```

This simple tag appears in `detail/transactions.macro` as the fourth parameter of every `TRANSACTION(...)` entry. It statically encodes the policy decision of whether a transaction type is safe to delegate in bulk. Sensitive transaction types — `ttACCOUNT_SET`, `ttREGULAR_KEY_SET` — are `notDelegable`, while most operational types (`ttPAYMENT`, `ttESCROW_CREATE`, `ttOFFER_CREATE`, etc.) are `delegable`.

---

## `Permission` Singleton

`Permission` is a Meyer's singleton (constructed on first call to `getInstance()`). Its private constructor populates five immutable `unordered_map` tables, all driven by the same two macro files:

- **`txFeatureMap_`** (`TxType → uint256`): Maps each transaction type to its enabling amendment hash. A zero `uint256` means the transaction requires no amendment — it is always available. The map is populated from `transactions.macro` using the `amendment` parameter.
- **`delegableTx_`** (`TxType → Delegation`): Maps each transaction type to its `delegable`/`notDelegable` tag, also from `transactions.macro`.
- **`granularPermissionMap_`** (`string → GranularPermissionType`): Bidirectional name lookup, string side. Used during JSON parsing to convert `"TrustlineFreeze"` → `65538`.
- **`granularNameMap_`** (`GranularPermissionType → string`): The reverse direction. Used during serialization to convert `65538` → `"TrustlineFreeze"`.
- **`granularTxTypeMap_`** (`GranularPermissionType → TxType`): Maps each granular permission to its parent transaction type. Used at execution time to determine which transactor context is relevant.

After construction the maps are read-only, so all concurrent reads from transaction processing threads are safe without locking.

---

## Key Methods

**`getPermissionName(uint32_t value)`** first tries to resolve the value as a `GranularPermissionType` via `granularNameMap_`. If that fails, it decodes the value as a transaction-level permission using `permissionToTxType()` and looks up the human-readable name from `TxFormats`. This unified lookup is what lets `STUInt32::getText()` and `STUInt32::getJson()` render any `sfPermissionValue` field as a string instead of a raw number.

**`isDelegable(uint32_t permissionValue, Rules const& rules)`** is the gatekeeper called by `DelegateSet` validation. Its logic has three layers: (1) if the value maps to a granular permission, it is always delegable — granular permissions are inherently restricted so there is no need to further qualify them; (2) for transaction-level permissions, the associated amendment must be enabled in the current ruleset — a delegate cannot be granted authority for a transaction type that isn't active on the network; (3) the transaction type must be marked `delegable` in `transactions.macro`.

**`getTxFeature(TxType)`** returns the amendment required by a transaction type, or `nullopt` if none. The assertion inside guards against calling this with a type absent from `txFeatureMap_`, which would represent a programming error (a transaction that exists in one macro file but not the other).

---

## Usage Across the Codebase

The permission system integrates at three distinct layers:

- **Submission validation** (`DelegateSet.cpp`): Iterates the `sfPermissions` array and calls `isDelegable()` on each value. Duplicate values are also rejected. This ensures that only legally delegable, amendment-enabled permissions can be stored on-ledger.
- **Execution** (`DelegateUtils.cpp`): `checkTxPermission()` verifies coarse transaction-level authorization by comparing `tx.getTxnType() + 1` against the stored permission array. `loadGranularPermission()` builds a set of active granular permissions for the relevant `TxType`, which the transactor then consults to allow or deny specific sub-operations.
- **Serialization** (`STInteger.cpp`, `STParsedJSON.cpp`): When serializing an `sfPermissionValue` to JSON, the numeric value is transparently replaced with its string name. When parsing JSON, a string value for `sfPermissionValue` is resolved back to its numeric representation via `getGranularValue()`. This round-trip ensures developer-facing JSON is always readable.