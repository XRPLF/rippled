# `Oracle.h` — Auto-Generated Oracle Ledger Entry Wrapper

## Role and Context

This file defines the C++ interface for the `ltORACLE` ledger entry type (type code `0x0080`), introduced by the XRP Ledger Price Oracle feature. It lives in `include/xrpl/protocol_autogen/ledger_entries/` alongside ~30 other auto-generated files, one per ledger entry type. The header comment makes this explicit: **do not edit** — the file is produced by a code generator that reads XRPL field and format definitions and emits type-safe wrappers so consumers never have to touch raw `SLE` field accessors directly.

The Oracle ledger object represents an on-chain price feed published by a trusted data provider. A single XRPL account can own multiple Oracle entries, each identified by an optional `sfOracleDocumentID`. The entry records a series of asset prices (`sfPriceDataSeries`), the provider's identity, the asset category being priced, and a Unix timestamp of the last update.

## Two-Class Structure: Reader and Builder

The file exports two classes into the `xrpl::ledger_entries` namespace: `Oracle` (the immutable reader) and `OracleBuilder` (the fluent construction interface). This split is a deliberate separation of concerns mirrored across every ledger entry in the autogen layer.

### `Oracle` — Immutable Wrapper

`Oracle` inherits from `LedgerEntryBase`, which holds a `std::shared_ptr<SLE const>` — a read-only reference to a Serialized Ledger Entry. The `const`-ness is enforced at the pointer's value type, not just the pointer itself, so the SLE cannot be mutated through this interface at all.

Construction takes a `shared_ptr<SLE const>` and immediately validates that the underlying SLE's type matches `ltORACLE`, throwing `std::runtime_error` on mismatch. This is an early-detection guard: code that accidentally wraps the wrong ledger object type fails loudly at construction rather than silently returning garbage from field getters.

All getters are marked `[[nodiscard]]` and `const`. The distinction between required and optional fields is enforced by the return type:

- **Required fields** (`sfOwner`, `sfProvider`, `sfPriceDataSeries`, `sfAssetClass`, `sfLastUpdateTime`, `sfOwnerNode`, `sfPreviousTxnID`, `sfPreviousTxnLgrSeq`) return their value type directly via `sle_->at(field)`.
- **Optional fields** (`sfOracleDocumentID`, `sfURI`) return `protocol_autogen::Optional<T>`, which resolves to `std::optional<T>` for value types or `std::optional<std::reference_wrapper<T>>` for reference types. Each optional getter is paired with a `has*()` predicate that calls `sle_->isFieldPresent()`.

`sfPriceDataSeries` is notable: it returns `STArray const&` via `getFieldArray()` rather than using the typed `at()` accessor, because STArray fields don't fit into the generic field template. This is called out in the comment as an "untyped field (unknown)" — the generator recognizes it cannot produce a strongly-typed accessor and falls back to the raw array access.

`sfProvider` and `sfAssetClass` are both `SF_VL` (variable-length blob) fields. In practice, `sfProvider` encodes the oracle provider's identifier (e.g., a string name or URL), and `sfAssetClass` names the category of assets being priced (e.g., `"currency"`). `sfURI` is an optional blob pointing to supplementary documentation.

### `OracleBuilder` — Fluent Construction

`OracleBuilder` inherits from `LedgerEntryBuilderBase<OracleBuilder>`, a CRTP template that provides common setters (`setFlags()`, `setLedgerIndex()`) returning `Derived&` for method chaining. The base class stores an internal `STObject object_{sfLedgerEntry}` — a free-form serialized object not bound to any template yet.

A critical design decision in the base constructor: it explicitly avoids calling `object_.set(soTemplate)`. If a template were applied early, the STObject would pre-populate `soeDEFAULT` fields as placeholders, and the subsequent `SLE` constructor's `applyTemplate()` call would throw "may not be explicitly set to default" for those fields. By keeping the internal object free until `build()`, the builder avoids this trap while still ensuring the final `SLE` is properly template-validated at construction time.

The primary `OracleBuilder` constructor takes all eight required fields as parameters and calls each corresponding `set*()` method immediately. There is also a second constructor that accepts an existing `std::shared_ptr<SLE const>` and copies it into `object_` via `*sle` dereferencing — this supports the edit-and-rebuild pattern where a caller reads an existing Oracle from the ledger, modifies it through the builder interface, and produces a new SLE.

`build(uint256 const& index)` finalizes construction by wrapping `std::move(object_)` and the provided ledger index into a new `SLE`, then constructing and returning an `Oracle` wrapper around it. The `std::move` here is significant: the builder is left in a moved-from state after `build()`, reinforcing single-use semantics.

## Relationship to Other Files

`LedgerEntryBase` provides `validate()` — calling `protocol_autogen::validateSTObject()` against the ledger format's `SOTemplate` — as well as the escape hatch `getSle()` returning the raw `shared_ptr<SLE const>` for contexts where type-safe accessors aren't sufficient.

`LedgerEntryBuilderBase` supplies `setFlags()` and `setLedgerIndex()` as the only mutable common-field operations; everything else is Oracle-specific. The `std::decay_t<typename SF_ACCOUNT::type::value_type>` pattern used in setter signatures strips references and cv-qualifiers from the canonical value types, ensuring setters accept both lvalues and rvalues without overload proliferation.

The `protocol_autogen::Optional<T>` alias in `Utils.h` is a narrow but important utility: it handles the case where a field's `value_type` is itself a reference (which `std::optional` cannot directly hold), transparently wrapping it in `std::reference_wrapper`. This keeps optional getter signatures consistent across all field types without requiring per-type specializations.