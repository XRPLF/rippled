# `FeeSettings.h` — Auto-Generated FeeSettings Ledger Entry Wrapper

## Role in the System

This auto-generated header (do not edit) defines the type-safe C++ interface for the `FeeSettings` ledger object (`ltFEE_SETTINGS`, type code `0x0073`). The `FeeSettings` ledger entry is a **singleton object** present in every XRPL ledger state — there is exactly one, and it stores the network's consensus-voted fee policy: the base transaction fee, owner reserve requirements, and their per-drop equivalents. Code that needs to read or write these network parameters uses this file's two classes: `FeeSettings` (immutable reader) and `FeeSettingsBuilder` (construction and mutation).

The file lives in `include/xrpl/protocol_autogen/ledger_entries/`, alongside a parallel class for every other ledger entry type on XRPL. The code-generation pattern exists to avoid handwritten, error-prone field access scattered throughout the codebase — every ledger entry type gets the same disciplined wrapper/builder pair, enforcing type safety and optionality at compile time.

## `FeeSettings` — Immutable Read Wrapper

`FeeSettings` extends `LedgerEntryBase`, which holds a `std::shared_ptr<SLE const>` (Serialized Ledger Entry). Immutability is enforced by wrapping a `const`-qualified `SLE`: callers cannot accidentally mutate the ledger state through this object.

The constructor validates the SLE type on construction, throwing `std::runtime_error` immediately if the wrapped `SLE` is not actually a `ltFEE_SETTINGS` entry. This is the primary defense against misuse — if code accidentally wraps the wrong entry type, it fails loudly at the call site rather than silently reading garbage field values later.

Every field exposed by `FeeSettings` is marked `soeOPTIONAL` in the protocol definition. As a result, every getter returns `protocol_autogen::Optional<T>`, which resolves to `std::optional<T>` for value types (or `std::optional<std::reference_wrapper<T>>` for reference types, via a `std::conditional_t` alias in `Utils.h`). Each getter is paired with a `has*()` predicate that directly calls `sle_->isFieldPresent(sf...)`. Callers are expected to check presence before dereferencing. The `[[nodiscard]]` attribute on every getter enforces that return values are not silently dropped.

### The Two-Generation Field Sets

A notable design feature is the dual representation of fee amounts. The entry exposes two parallel groups of fields:

- **Legacy integer fields**: `sfBaseFee` (uint64), `sfReferenceFeeUnits` (uint32), `sfReserveBase` (uint32), `sfReserveIncrement` (uint32). These predate the `XRPFees` amendment and encode fees in "fee units" — an abstraction layer where 1 XRP equals a fixed number of fee units.
- **New drops-denominated fields**: `sfBaseFeeDrops`, `sfReserveBaseDrops`, `sfReserveIncrementDrops`, all typed as `STAmount`. Post `XRPFees` amendment, only the `*Drops` fields are populated, and the `Change` transactor in `Change.cpp` enforces that these fields must be present when the amendment is active.

Exposing both groups as optional fields on the same class allows consumers to handle both pre- and post-amendment ledger states without branching on the type system — the `has*()` predicates serve as the runtime switch.

`sfPreviousTxnID` (uint256) and `sfPreviousTxnLgrSeq` (uint32) complete the field set, tracking the hash and ledger sequence of the last transaction that modified this entry — a standard audit trail carried by many ledger objects.

## `FeeSettingsBuilder` — Fluent Construction

`FeeSettingsBuilder` extends `LedgerEntryBuilderBase<FeeSettingsBuilder>`, which uses CRTP to return the concrete `FeeSettingsBuilder&` from every inherited setter, making fluent chaining type-correct without virtual dispatch or casting at the call site.

The base class initializes an internal `STObject object_{sfLedgerEntry}` and sets `sfLedgerEntryType` and `sfFlags` in the constructor, but deliberately does **not** call `object_.set(soTemplate)`. The comment in `LedgerEntryBuilderBase` explains why: calling `set(soTemplate)` would create `STBase` placeholders for `soeDEFAULT` fields, which causes `applyTemplate()` to throw "may not be explicitly set to default" when the `SLE` is subsequently constructed. By omitting that call, the builder leaves the `STObject` as a free object; the `SLE` constructor applies the template correctly when `build()` is called.

The builder provides two construction paths:
1. **Default construction** — starts fresh with only `sfLedgerEntryType` and `sfFlags`.
2. **From existing SLE** — copies the `SLE`'s fields into `object_` (via `object_ = *sle`), allowing modification of an existing entry before reconstructing it. The SLE-based constructor validates the entry type and throws on mismatch, matching the `FeeSettings` wrapper's own guard.

The `build(uint256 const& index)` method moves `object_` into a new `SLE` keyed by `index`, wraps it in a `shared_ptr<SLE const>`, and constructs the final `FeeSettings` wrapper. After `build()` the builder's internal state has been moved from and should not be reused.

## Validation

Both `FeeSettings` and `FeeSettingsBuilder` inherit `validate()`, which compares the current field population against the `SOTemplate` retrieved from `LedgerFormats::getInstance()`. This confirms that required fields are present and no invalid fields are set — useful in tests and as an assertion before persisting an entry.

The unit tests in `FeeSettingsTests.cpp` verify all four critical invariants: builder-setter round-trips, SLE-to-builder-to-wrapper round-trips, type mismatch exceptions from both the wrapper and the builder, and that absent optional fields correctly return `std::nullopt`.