## `include/xrpl/ledger/helpers/DelegateHelpers.h`

This header is part of the XRPL delegate account system, which allows one account to authorize another — a "delegate" — to submit certain transactions on its behalf with explicitly bounded permissions. The file declares the two functions that sit at the enforcement layer of that system: determining whether a transaction type is permitted at all, and loading the fine-grained permission flags that constrain what the delegate can do within that transaction type.

### Context and motivation

The delegate model solves a practical custody and automation problem. An account holder may want a hot wallet or automated agent to submit routine transactions without granting it full signing authority. Rather than relying solely on multisig or regular key mechanisms, XRPL's delegate system lets an account enumerate exactly which transaction types — and even which field-level sub-operations within those types — the delegate may exercise. `DelegateHelpers.h` provides the runtime lookup functions that individual transaction appliers invoke during permission validation.

The underlying ledger state lives in a `Delegate` object (type `ltDELEGATE`), which records the authorizing account (`sfAccount`), the delegate account (`sfAuthorize`), and an `sfPermissions` array. Each element of that array holds an `sfPermissionValue`: an integer that encodes either a transaction-type permission (the `TxType` value plus one, by convention) or a granular sub-operation flag (a `GranularPermissionType` value, defined to exceed the maximum `uint16` to avoid collisions with transaction types).

### `checkTxPermission`

`checkTxPermission` answers the binary question: is this transaction type permitted by the delegate relationship at all? It receives the `Delegate` ledger state entry as a `shared_ptr<SLE const>` — a null pointer returns `terNO_DELEGATE_PERMISSION` immediately, acting as a guard against a missing ledger object. It then reads `sfPermissions` from the SLE and linearly scans for an entry whose `sfPermissionValue` equals `tx.getTxnType() + 1`, the transaction-type permission encoding.

The return type is `NotTEC`, the XRPL error-code subset that excludes `TEC` (claimed fee) codes but includes both success codes and `ter` (retryable) codes. The meaningful outcomes are `tesSUCCESS` and `terNO_DELEGATE_PERMISSION`. Choosing a `ter` code rather than a `tef` (final) code is deliberate: the delegate ledger entry could change in a subsequent ledger, potentially making an identical transaction valid in the future without any modification. A `ter` result invites retry; a `tef` would definitively reject it.

The `shared_ptr<SLE const>` convention for ledger object handles throughout XRPL ensures the delegate entry stays alive through the check without copying the object, while `const` prevents accidental mutation during this read-only operation.

### `loadGranularPermission`

Once `checkTxPermission` has confirmed that the transaction type is permitted in the broad sense, some transaction types support a second dimension of delegation: granular flags that authorize only specific sub-operations. For example, a delegate authorized for `TrustSet` might still be restricted to only the `TrustlineAuthorize` flag, or a delegate for `AccountSet` might only be permitted to modify specific account settings.

`loadGranularPermission` walks the same `sfPermissions` array, but instead of matching on transaction-type values, it casts each `sfPermissionValue` to `GranularPermissionType` and asks `Permission::getInstance().getGranularTxType()` whether that granular type maps to the requested `TxType`. If it does, the value is inserted into the caller-provided `unordered_set<GranularPermissionType>`.

The design choice of populating a caller-owned set rather than returning a freshly allocated one is significant. Callers in practice (e.g., `TrustSet::checkPermission`, `Payment::checkPermission`, `AccountSet::checkPermission`) declare the set on the stack and pass it in by reference, avoiding heap allocation. It also allows future callers to accumulate granular permissions from multiple calls in sequence if needed, though current usage calls it once per permission check.

### Call pattern in transactors

The two functions are always used in sequence. A typical transactor's `checkPermission` method first resolves the `Delegate` SLE from the ledger via `keylet::delegate`, guards against a missing object, then calls `checkTxPermission`. If that returns success, the permission check exits early — no granular inspection is needed. If it fails, the transactor falls through to `loadGranularPermission`, populates the granular set, and then checks each flag against the transaction's field values (e.g., `tfSetfAuth`, `tfSetFreeze` for TrustSet; `PaymentXRP`, `PaymentIOU` for Payment).

This two-stage pattern keeps the fast path efficient: transactions whose delegate holds a blanket transaction-type permission skip the entire granular scan. The granular path is only entered when the delegate relationship is more restrictively configured, which is the exceptional case in practice.

### Implementation location

The implementations of both functions live in `src/libxrpl/tx/transactors/delegate/DelegateUtils.cpp`. The permission schema — the `GranularPermissionType` enum, the mapping from granular values to transaction types, and the `txToPermissionType` encoding convention — is centralized in `xrpl/protocol/Permissions.h`, keeping schema definition separate from runtime enforcement.