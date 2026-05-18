# `MPTokenIssuance.h` — Auto-generated Ledger Entry Wrapper for MPT Issuances

This file lives in the `xrpl::ledger_entries` namespace and is part of a machine-generated type-safe layer over the XRPL ledger's raw serialized-object infrastructure. It defines two classes — `MPTokenIssuance` and `MPTokenIssuanceBuilder` — that represent the `ltMPTOKEN_ISSUANCE` ledger entry type (wire code `0x007e`), which was introduced as part of XRPL's Multi-Purpose Token (MPT) feature.

## Role in the MPT System

XRPL's MPT feature introduces a more compact and efficient fungible-token model relative to the older trust-line / IOU approach. The on-ledger data for this model is split between two complementary entry types: `MPTokenIssuance` (this file, `0x007e`) describes the global parameters of a token class — its issuer, supply cap, outstanding supply, transfer fee, and metadata — while `MPToken` (`0x007f`, in the sibling `MPToken.h`) records the balance held by a single account. The relationship is one-to-many: one `MPTokenIssuance` anchors every individual `MPToken` holder entry, which references it via `sfMPTokenIssuanceID`.

## `MPTokenIssuance`: Immutable Read Wrapper

`MPTokenIssuance` inherits from `LedgerEntryBase`, which holds a `std::shared_ptr<SLE const>` — the `const` qualifier is load-bearing. This class is deliberately read-only: no setter exists, and the underlying `SLE` cannot be mutated through this interface. Every getter is marked `[[nodiscard]]`.

The constructor accepts a `std::shared_ptr<SLE const>` and immediately checks that `sle_->getType() == ltMPTOKEN_ISSUANCE`, throwing `std::runtime_error` on mismatch. This guard converts a latent protocol-level invariant (only the right SLE type should ever be wrapped) into a hard C++ exception at the boundary, so callers don't silently operate on the wrong entry type.

### Field Categories and Access Pattern

Fields fall into three XRPL schema categories, each with a distinct access pattern:

**`soeREQUIRED`** fields (`sfIssuer`, `sfSequence`, `sfOwnerNode`, `sfOutstandingAmount`, `sfPreviousTxnID`, `sfPreviousTxnLgrSeq`) are always present on any well-formed entry. Their getters return the value type directly with no `Optional` wrapper.

**`soeDEFAULT`** fields (`sfTransferFee`, `sfAssetScale`, `sfMutableFlags`) have a protocol-defined default value (typically zero) when absent from the serialized bytes. The code nonetheless exposes them via `protocol_autogen::Optional<T>` and a paired `has*()` predicate, treating presence as meaningful. This is intentional: a missing `sfTransferFee` field means "no fee configured" rather than "zero fee", a distinction that can matter for validation logic.

**`soeOPTIONAL`** fields (`sfMaximumAmount`, `sfLockedAmount`, `sfMPTokenMetadata`, `sfDomainID`) may genuinely be absent. All four follow the same pattern: the getter checks `isFieldPresent()` and returns `std::nullopt` if the field is missing. Callers must explicitly handle the optional.

The `protocol_autogen::Optional<T>` alias (defined in `Utils.h`) is a thin conditional: if `T` is a reference type it wraps `std::reference_wrapper<T>` inside `std::optional`, otherwise it is plain `std::optional<T>`. This avoids the undefined behavior of `std::optional` holding a reference.

### Notable Fields

`sfOutstandingAmount` tracks the aggregate of all tokens currently in circulation across every holder's `MPToken` entry. It is always present and required, not optional, because any valid issuance must track supply.

`sfLockedAmount` at the issuance level is an optional aggregate cache of locked balances held by individual `MPToken` holders (e.g., for escrow or DEX offers). Caching this at the issuance layer avoids needing to scan all holder entries to determine how much supply is encumbered.

`sfAssetScale` (a `uint8`) records the number of decimal places between the token's base unit and the minimum representable quantity. A value of `6` means the internal `uint64` counter represents millionths of the display unit. This is absent when the token uses its raw integer representation directly.

`sfMutableFlags` is structurally distinct from the standard `sfFlags` field inherited from `LedgerEntryBase`. The ledger separates flags that are fixed at issuance time (stored in `sfFlags`) from flags the issuer may change post-creation (stored in `sfMutableFlags`). Both are `uint32`, but only `sfMutableFlags` may be updated by subsequent transactions.

`sfDomainID` optionally ties the issuance to a `PermissionedDomain` ledger entry, restricting which accounts may hold this token. Its type is `uint256`, identifying the domain entry by its ledger key.

## `MPTokenIssuanceBuilder`: Fluent Construction

`MPTokenIssuanceBuilder` inherits from the CRTP base `LedgerEntryBuilderBase<MPTokenIssuanceBuilder>`. The CRTP parameterization lets the base-class setters (`setFlags()`, `setLedgerIndex()`) return `Derived&` rather than `LedgerEntryBuilderBase&`, preserving the concrete type across the method chain so callers never need to cast.

The primary constructor enforces the full required-field contract at construction time by accepting all six required fields as parameters and calling their setters immediately. Optional and default fields are set only if explicitly called afterward. This design prevents the builder from ever producing an SLE that is missing a required field, moving the validation error from `build()` time to construction time.

The `STObject`-based internal representation (`object_` of type `STObject sfLedgerEntry`) is kept deliberately "free" — the base class comment notes that calling `object_.set(soTemplate)` is avoided. Setting a template would pre-populate `soeDEFAULT` fields as STBase placeholder objects, which would then cause the `SLE` constructor's `applyTemplate()` call to reject them with "may not be explicitly set to default". By starting field-free and only writing fields that are actually set, the builder remains compatible with `SLE`'s template validation.

The secondary constructor accepts `std::shared_ptr<SLE const>` and copies the dereferenced `SLE` into `object_` via `object_ = *sle`. This enables a read-modify pattern: wrap an existing ledger entry in a builder, adjust fields, and produce a new entry via `build()`.

`build(uint256 const& index)` consumes the builder's internal `STObject` via `std::move` and hands it — along with the entry's ledger key — to the `SLE` constructor, then wraps the result in an `MPTokenIssuance`. After `build()`, the builder's `object_` is in a moved-from state and should not be reused.

## Relationship to the Autogeneration System

The header comment "This file is auto-generated. Do not edit." signals that the source of truth for `MPTokenIssuance`'s field set is a schema elsewhere in the build system. Every ledger entry in `ledger_entries/` follows the same `Wrapper + Builder` pattern, generated from the same machinery. The uniform structure — immutable wrapper with `get*()` / `has*()` pairs, fluent builder with `set*()` methods, CRTP base — means any new field in the schema automatically produces consistent accessors without hand-written boilerplate or risk of omission.