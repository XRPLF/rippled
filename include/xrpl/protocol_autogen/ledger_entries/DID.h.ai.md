# `DID.h` — Decentralized Identifier Ledger Entry Wrapper

Auto-generated file in `include/xrpl/protocol_autogen/ledger_entries/` that provides the `DID` read-only wrapper and `DIDBuilder` construction interface for the XRPL DID ledger entry type (`ltDID`, 0x0049). It sits in the `xrpl::ledger_entries` namespace alongside ~30 other type-specific files that follow the identical code-generation pattern.

## What Problem It Solves

The raw XRPL ledger representation (`SLE` — Serialized Ledger Entry) is untyped: any field can be read from any entry without compile-time guarantees. This file and its siblings impose a typed API on top of that dynamism. Code that holds a `DID` object knows it wraps exactly an `ltDID` entry and can call `getAccount()`, `getDIDDocument()`, etc., without spelling out `sfAccount` or `sfDIDDocument` literals or performing manual presence checks.

## The DID Ledger Entry

A DID entry anchors a W3C Decentralized Identifier on the XRPL. It carries:

- **`sfAccount`** (required) — the XRPL account that owns this identifier.
- **`sfDIDDocument`** (optional) — the raw DID document body, stored inline as a variable-length blob (`SF_VL`).
- **`sfURI`** (optional) — a URI pointing to the DID document when it is stored off-ledger.
- **`sfData`** (optional) — arbitrary attestation or metadata blob associated with the identifier.
- **`sfOwnerNode`** (required) — back-pointer into the account's owner directory page, needed for efficient deletion.
- **`sfPreviousTxnID` / `sfPreviousTxnLgrSeq`** (required) — standard audit trail fields present on every mutable ledger entry.

The three optional fields (`sfDIDDocument`, `sfURI`, `sfData`) reflect the DID spec's flexibility: a DID can store its document inline, reference it externally via URI, or carry opaque data — or any combination.

## `DID` — Immutable Read Wrapper

`DID` extends `LedgerEntryBase`, which holds a `std::shared_ptr<SLE const>` and exposes common field getters (`getType()`, `getKey()`, `getFlags()`, `getLedgerIndex()`). The subclass adds entry-specific accessors.

Type safety is enforced eagerly in the constructor: it compares `sle_->getType()` against the `static constexpr LedgerEntryType entryType = ltDID` sentinel and throws `std::runtime_error` on mismatch. This makes it impossible to accidentally wrap an `Offer` or `AccountRoot` in a `DID` handle; the error surfaces at the point of construction, not at a later field read.

Required fields (`getAccount()`, `getOwnerNode()`, `getPreviousTxnID()`, `getPreviousTxnLgrSeq()`) return their native C++ value types directly via `sle_->at(sf*)`. Optional fields return `protocol_autogen::Optional<SF_VL::type::value_type>` — a thin alias for `std::optional` — and are paired with explicit `has*()` predicates (`hasDIDDocument()`, `hasURI()`, `hasData()`). The getter delegates to the predicate internally before calling `sle_->at(...)`, avoiding an unconditional access that would throw for absent fields.

All getters are marked `[[nodiscard]]` and `const`, reinforcing the immutability contract.

## `DIDBuilder` — Fluent Construction Interface

`DIDBuilder` inherits from the CRTP base `LedgerEntryBuilderBase<DIDBuilder>`, which provides `setFlags()` and `setLedgerIndex()` returning `Derived&` — enabling method chaining without virtual dispatch overhead.

The constructor requires the four mandatory fields (account, ownerNode, previousTxnID, previousTxnLgrSeq) and immediately calls their setters, ensuring the entry is valid at minimum even before optional fields are added. A second constructor accepts an existing `SLE const` for the edit-then-rebuild pattern, verifying the entry type before copying the underlying `STObject`.

A subtle but important design point lives in `LedgerEntryBuilderBase`: the internal `STObject object_{sfLedgerEntry}` is kept as a *free object* — never bound to a SOTemplate. Calling `object_.set(soTemplate)` would create `STBase` placeholders for `soeDEFAULT` fields, which would later cause the `SLE` constructor's `applyTemplate()` to throw *"may not be explicitly set to default"*. By omitting template binding, the builder accumulates only the fields that are explicitly set and leaves the SLE constructor responsible for handling all defaults.

`build(uint256 const& index)` finalizes construction: it moves the assembled `STObject` into an `SLE` via `std::make_shared<SLE>(std::move(object_), index)`, then wraps that SLE in a `DID` instance returned by value. After `build()`, the builder's internal state is consumed (moved out), so it should not be reused.

Both classes expose a `validate()` method that delegates to `protocol_autogen::validateSTObject`, cross-checking the accumulated or wrapped fields against the canonical `SOTemplate` from `LedgerFormats`. The auto-generated tests exercise the full round-trip: builder → `build()` → `DID` getters, as well as SLE-construction and wrong-type rejection.

## Relationship to Other Files

`DID.h` is purely a projection of the same ledger entry schema defined elsewhere. The `sfDIDDocument`, `sfURI`, `sfData`, and `sfAccount` field descriptors come from `SField.h`; the `ltDID` constant comes from `LedgerFormats.h`. The auto-generation tooling reads the canonical field schema and emits one file per entry type — `Credential.h`, `Oracle.h`, `Offer.h`, and so on all follow the identical two-class pattern with `LedgerEntryBase` and `LedgerEntryBuilderBase<T>` as the shared infrastructure. Nothing in this file should ever be edited by hand.