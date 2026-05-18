# `DelegateUtils.cpp` — Delegate Permission Enforcement Utilities

This file is the execution-time core of XRPL's account delegation permission system. It implements two free functions that transactors call when processing a transaction submitted by a delegate (an account acting on behalf of another account). The broader delegation system allows account owners to grant either broad (per-transaction-type) or narrow (operation-specific) authority; these two functions are responsible for querying the on-ledger permission grant and determining whether a specific action is authorized.

## Encoding Convention: Why `TxType + 1`

The `sfPermissions` array stored on a delegate's `SLE` (Serialized Ledger Entry) uses a single `sfPermissionValue` integer field to encode both coarse-grained and granular permissions in the same space. Coarse permissions are encoded as `TxType + 1` — the `+1` shift is deliberate: `TxType` values start at zero (e.g., `ttPAYMENT == 0`), but a stored value of `0` would be ambiguous or falsy in some contexts. Adding one maps every valid transaction type to a positive non-zero integer occupying the range `[1, 65535]`. Granular permissions, by design, are assigned values above `UINT16_MAX` (≥ 65536), so the two namespaces never overlap. The `Permission` singleton asserts this separation at construction time.

## `checkTxPermission()`

This function answers the binary question: *does this delegate have authorization to submit this transaction type at all?* It takes the delegate's `SLE` (already fetched by the caller) and the pending `STTx`. It guards immediately against a null delegate, returning `terNO_DELEGATE_PERMISSION` — a `TER` (Transaction Error Result) code that signals a retriable protocol-level failure, distinct from a hard `TEC` error.

The check is a linear scan of the `sfPermissions` array. For each entry it reads `sfPermissionValue` and compares against `tx.getTxnType() + 1`. An empty array, a revoked delegate, or a permission list that simply does not include the required transaction type all produce the same outcome: `terNO_DELEGATE_PERMISSION`. The absence of short-circuit logic beyond the first match is intentional given that permission lists are expected to be small (bounded by ledger object size constraints).

## `loadGranularPermission()`

Where `checkTxPermission` handles the coarse gate, `loadGranularPermission` populates the set of *fine-grained* capabilities the delegate holds for a specific transaction type. Granular permissions represent sub-operations within a transaction — for example, a delegate might be granted `PaymentMint` (allowing only IOU issuance payments) without receiving authority over the full `Payment` transaction type.

The function iterates the same `sfPermissions` array but applies a different test. Each stored `permissionValue` is cast via `static_cast<GranularPermissionType>` and then passed to `Permission::getInstance().getGranularTxType()`, which consults the singleton's internal reverse map (`granularTxTypeMap_`) to find which `TxType` a given granular permission belongs to. Only entries whose parent `TxType` matches the requested `txType` are inserted into the output `granularPermissions` set. This design means individual transactors (e.g., `Payment`, `TrustSet`, `AccountSet`) can call `loadGranularPermission` with their own type and receive only the relevant sub-permissions, without needing to know the encoding details of unrelated permission namespaces.

The `static_cast` without a range check is a known tradeoff: the ledger's own validator rejects `sfPermissionValue` integers that do not correspond to registered entries at submission time, so by the time these functions execute the values are considered trusted.

## Null-Safety Pattern

Both functions follow the same null-guard idiom at their respective entry points. A null `delegate` pointer indicates the ledger lookup failed to find a delegate object (the account has not been granted delegation), which is semantically identical to having no permissions. `checkTxPermission` returns `terNO_DELEGATE_PERMISSION` while `loadGranularPermission` returns silently, leaving the output set empty. The callers can then handle both cases uniformly without needing to differentiate between "no delegate exists" and "delegate exists but has no relevant permission."

## Relationship to `DelegateHelpers.h` and the `Permission` Singleton

The file includes only `DelegateHelpers.h` and `STArray.h`. `DelegateHelpers.h` provides the function declarations and brings in `GranularPermissionType`, `Permission`, `TxType`, and the relevant `sfField` descriptors. The `Permission` singleton, initialized lazily on first access, owns the authoritative maps between granular permission codes and their parent transaction types — `loadGranularPermission` delegates all knowledge of that mapping back to the singleton rather than encoding it locally. This separation keeps the utility functions stateless and the permission registry centralized.