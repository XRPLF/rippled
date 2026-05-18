# `Bridge.h` — Auto-Generated `ltBRIDGE` Ledger Entry Wrapper

## Role in the System

This file is part of a code-generated layer under `xrpl/protocol_autogen/ledger_entries/` that provides type-safe C++ wrappers for every XRPL ledger object type. The `Bridge` class (type code `ltBRIDGE`, `0x0069`) represents the on-ledger anchor for an XRP Ledger cross-chain bridge — the persistent state object that records which chains are bridged, tracks claim sequence counters, and holds the reward configuration for attestation servers.

The `// This file is auto-generated. Do not edit.` comment at line 1 defines the contract: the schema for `ltBRIDGE` lives in `ledger_entries.macro`, and regenerating this file from that source is the correct way to evolve it. Hand-editing would immediately diverge from the canonical schema and would be overwritten on the next code generation pass.

## Why This Layer Exists

The underlying storage type, `SLE` (Serialized Ledger Entry), is a generic property-bag accessed via `SF_*` field descriptors with no compile-time type safety. Accessing a field that doesn't exist, accessing it under the wrong type, or forgetting to check for optional presence are all silent bugs at the `SLE` level. The `Bridge` wrapper eliminates that surface: every field has a named getter with a concrete return type derived from its `SF_*` descriptor's `value_type`, optional fields return `protocol_autogen::Optional<T>` (an alias for `std::optional`), and the wrapper refuses construction on type mismatch.

## `Bridge` — Immutable Read View

`Bridge` inherits from `LedgerEntryBase`, which holds the `shared_ptr<SLE const> sle_` member and contributes getters for the universal fields present on every ledger entry: `getKey()`, `getType()`, `getFlags()`, `getLedgerEntryType()`, and `getLedgerIndex()`. `Bridge` adds the entry-specific accessors.

The constructor takes ownership of a `shared_ptr<SLE const>` and immediately validates the type against `entryType = ltBRIDGE`, throwing `std::runtime_error` on mismatch. This fail-fast design prevents silent type confusion: it is impossible to construct a `Bridge` wrapping a `Check` or `Escrow` SLE and then read misinterpreted field data from it.

The cross-chain-specific fields exposed by `Bridge` map directly to the schema defined in the `LEDGER_ENTRY(ltBRIDGE, ...)` macro:

- **`getXChainBridge()`** — Returns the `SF_XCHAIN_BRIDGE` value that identifies the locking and issuing chains. This is the bridge's defining identity.
- **`getSignatureReward()`** — An `STAmount` denominating the XRP reward paid to witness servers that submit valid attestations.
- **`getMinAccountCreateAmount()`** / **`hasMinAccountCreateAmount()`** — The sole optional field (`soeOPTIONAL`). A bridge operator may or may not require a minimum deposit when creating accounts via the bridge. The dual pattern — a separate `has*` predicate alongside a conditional getter returning `std::nullopt` — makes optional field handling explicit and prevents callers from accidentally calling `sle_->at()` on an absent field, which would throw.
- **`getXChainClaimID()`**, **`getXChainAccountCreateCount()`**, **`getXChainAccountClaimCount()`** — Three `uint64` counters. `XChainClaimID` is a monotonically-increasing counter used to generate unique IDs for `XChainOwnedClaimID` entries. `XChainAccountCreateCount` and `XChainAccountClaimCount` track account-creation and account-claim operations, respectively. These sequence values are critical to the bridge's replay-prevention and ordering guarantees.
- **`getAccount()`**, **`getOwnerNode()`**, **`getPreviousTxnID()`**, **`getPreviousTxnLgrSeq()`** — Standard bookkeeping fields present on nearly all modifiable ledger objects: the owning account, the owner-directory page index, and the last transaction that touched the entry.

All getters are `[[nodiscard]] const`, enforcing that `Bridge` is a purely read-only view with no mutation interface.

## `BridgeBuilder` — Fluent Construction via CRTP

`BridgeBuilder` inherits from `LedgerEntryBuilderBase<BridgeBuilder>`, a CRTP base that holds the mutable `STObject object_` and provides `setFlags()`, `setLedgerIndex()`, and the `validate()` method. The CRTP parameter ensures that these inherited setters return `BridgeBuilder&`, preserving the fluent chaining interface without virtual dispatch.

The base class constructor notably avoids calling `object_.set(soTemplate)` on the `STObject`. This is a deliberate design choice documented in `LedgerEntryBuilderBase.h`: applying the SO template early would insert placeholder `STBase` values for `soeDEFAULT` fields, and when the `SLE` constructor subsequently calls `applyTemplate()` it would throw "may not be explicitly set to default." By keeping `object_` as a template-free `STObject` and letting the `SLE` constructor handle template application, the builder avoids this subtle ordering constraint.

`BridgeBuilder` exposes two construction paths:

1. **From required field values** — All nine required fields must be supplied to the primary constructor, which immediately calls the corresponding `set*` methods. This front-loads field enforcement: there is no way to reach `build()` without having provided every required field.

2. **From an existing `SLE const`** — The second constructor copies the SLE's data into `object_`, enabling a read-modify-write workflow: deserialize an existing `Bridge` entry, mutate specific fields via the fluent setters, and build a new `SLE`. Type validation is applied immediately here too.

`setMinAccountCreateAmount()` is intentionally absent from the primary constructor, as it is optional. Callers who need it chain it after construction: `BridgeBuilder{...}.setMinAccountCreateAmount(amount).build(index)`.

`build(uint256 const& index)` finalizes construction by moving `object_` into a new `SLE` at the given ledger key, then wrapping it in a `Bridge`. The index must be externally computed — typically as a deterministic hash of the bridge's defining parameters — because the builder itself has no knowledge of the ledger's key scheme.

## Fit in the Autogen Pattern

This file is structurally identical to every sibling in `ledger_entries/` — `AMM.h`, `Escrow.h`, `Check.h`, and the rest. The uniformity is the point: a single code generator produces consistent, auditable wrapper pairs for every ledger entry type. Adding a new field to `ltBRIDGE` in the macro means regenerating this file, not patching it by hand. Callers working at a higher abstraction level see a clean typed interface; the raw `SLE` is accessible via `getSle()` on `LedgerEntryBase` for the rare cases where the generated accessors are insufficient.