# `include/xrpl/protocol_autogen/ledger_entries/Delegate.h`

## Purpose and Domain Context

This file is an auto-generated header in the `xrpl::ledger_entries` namespace that defines the C++ surface for the `ltDELEGATE` (type code `0x0083`) on-ledger object. The `Delegate` ledger entry is the persistent record of an account permission delegation relationship: it records that one XRPL account (`sfAccount`) has authorized another (`sfAuthorize`) to submit a constrained set of transactions on its behalf, with the permitted operations encoded as an `sfPermissions` array.

The ledger entry exists to support a custody and automation pattern. Rather than sharing a private key or registering a regular key with full signing authority, an account owner submits a `DelegateSet` transaction to create or modify a `Delegate` object. The resulting object is what the enforcement layer — primarily `checkTxPermission()` and `loadGranularPermission()` in `DelegateHelpers.h` — reads at transaction-apply time to decide whether a delegate-submitted operation is allowed.

## Class Design: Immutable Wrapper

`Delegate` extends `LedgerEntryBase`, a thin base class that holds a `std::shared_ptr<SLE const>` as its only data member. The `const`-qualified pointer is the architectural commitment: once constructed, no field on the wrapped `SLE` can be mutated through this class. All six getter methods — `getAccount()`, `getAuthorize()`, `getPermissions()`, `getOwnerNode()`, `getPreviousTxnID()`, and `getPreviousTxnLgrSeq()` — are `[[nodiscard]] const` accessors that delegate directly to `sle_->at(sf...)` or `sle_->getFieldArray(sf...)`.

The constructor takes a `std::shared_ptr<SLE const>` and immediately verifies `sle_->getType() == ltDELEGATE`, throwing `std::runtime_error` on mismatch. This is a defensive invariant: the auto-generation framework ensures the type check is always present, making it impossible to accidentally wrap an `AccountRoot` SLE inside a `Delegate` accessor without a hard failure.

### The `sfPermissions` Asymmetry

Five of the six fields are strongly typed primitives — `SF_ACCOUNT`, `SF_UINT64`, `SF_UINT256`, `SF_UINT32` — and their getters return their underlying `value_type` directly via `sle_->at()`. The `sfPermissions` field is different: it returns `STArray const&` via `sle_->getFieldArray()`. The inline comment acknowledges this is an "untyped field (unknown)," reflecting that `STArray` is a heterogeneous sequence of `STObject` entries, each containing an `sfPermissionValue` integer. The distinction matters because the permission system uses a two-tier numeric encoding: values in `[1, UINT16_MAX]` represent transaction-type permissions, while values above `UINT16_MAX` represent granular sub-operation flags (see `Permissions.h`). That structured complexity cannot be captured by a scalar `SField` type, so the raw `STArray` reference is surfaced and callers are expected to iterate it themselves.

## Class Design: Fluent Builder

`DelegateBuilder` extends `LedgerEntryBuilderBase<DelegateBuilder>` using CRTP, which allows the base class's `setLedgerIndex()` and `setFlags()` methods to return `DelegateBuilder&` for method chaining without requiring virtual dispatch or casts at call sites.

The base class stores an `STObject object_{sfLedgerEntry}` rather than calling `object_.set(soTemplate)` during construction. The comment in `LedgerEntryBuilderBase` explains the deliberate non-call: invoking the template would pre-populate `soeDEFAULT` fields with placeholder `STBase` values, which would later cause `applyTemplate()` — called from the `SLE` constructor — to throw "may not be explicitly set to default." Keeping the object template-free avoids that trap while still allowing the `SLE` constructor to handle field defaults correctly.

`DelegateBuilder` provides two construction paths. The primary one accepts all six required field values and calls each setter sequentially. The secondary one accepts an `std::shared_ptr<SLE const>` and copies the SLE's field data into the mutable `object_`, enabling in-place modification of an existing ledger entry (useful when `DelegateSet` needs to update a pre-existing delegate relationship). Both paths validate the ledger entry type and throw on mismatch.

The terminal method, `build(uint256 const& index)`, creates a new `SLE` from the mutable `STObject` and the provided ledger index, then wraps it in a `Delegate` instance. The `std::move(object_)` means the builder is consumed by this call — it cannot be reused after `build()`, which is consistent with the builder-as-factory idiom.

## Position in the Auto-Generated Layer

All files in `include/xrpl/protocol_autogen/ledger_entries/` follow the same generated pattern: a read-only wrapper class paired with a builder class, both deriving from the same base templates. The `Delegate.h` content is structurally identical to siblings like `DepositPreauth.h` or `Credential.h`, differing only in entry type, field names, and field types. This uniformity is the direct benefit of code generation: adding a new ledger entry type to the schema produces a correct, consistently structured header without manual authorship.

The keylet binding for this entry type is `keylet::delegate(AccountID const& account, AccountID const& authorizedAccount)`, declared in `Indexes.h`. The two-account key structure reflects the uniqueness constraint: only one `Delegate` object can exist per `(grantor, grantee)` pair, so the key derivation hashes both account IDs together.