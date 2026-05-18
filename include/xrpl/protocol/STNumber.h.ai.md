# `STNumber.h` — Serializable Asset-Contextual Number

## Role in the System

`STNumber` solves a precise storage problem in XRPL's ledger model: how to store a numeric quantity whose asset type is *implicit from context* rather than embedded in the value itself. `STAmount` — the more familiar sibling — bundles a `Number` together with an `Asset` (XRP, IOU, or MPT) inside every ledger field. For objects like `Vault`, `LoanBroker`, and `Loan`, which have a single governing asset, repeating that asset in every field wastes space and couples each field value to asset data that already lives in the same object. `STNumber` eliminates that redundancy: it stores *only* the numeric value while the asset identity is injected at runtime through the `STTakesAsset` mechanism.

## Class Hierarchy and Double Inheritance

`STNumber` inherits from two bases: `STTakesAsset` (which itself extends `STBase`) and `CountedObject<STNumber>`. The `STBase` side provides the full serialization contract — `getSType()`, `add()`, `getText()`, `isEquivalent()`, `isDefault()`, and the placement-new `copy()`/`move()` pair. `CountedObject` is a lightweight RAII wrapper for memory diagnostics. The `STTakesAsset` layer adds a single data member, `std::optional<Asset> asset_`, which is populated at runtime and deliberately never written to the ledger.

## The `STTakesAsset` Contract and Asset Association

`STTakesAsset` defines the abstract concept of a serialization type that *may* hold a transient asset reference. The association point is `associateAsset(Asset const&)`. `STNumber`'s override does two things: it stores the asset in `asset_` (inherited), and it immediately calls `roundToAsset(a, value_)` to round the internal `Number` to the precision that asset allows. This is a key design invariant — after association, the stored value is already rounded, so `add()` can assert that a second rounding during serialization produces no change.

The flag `SField::sMD_NeedsAsset` is declared on all `NUMBER`-type SFields in `sfields.macro` (e.g., `sfAssetsAvailable`, `sfAssetsTotal`, `sfDebtTotal`, `sfPrincipalOutstanding`). The free function `associateAsset(STLedgerEntry&, Asset&)` iterates a ledger entry's fields, finds those with `sMD_NeedsAsset`, and calls `associateAsset` on each. Transactors for `Vault`, `LoanBroker`, and `Loan` invoke this near the end of `doApply()` after all modifications are complete — this is the intended association lifecycle.

## Serialization Wire Format

On the wire, `STNumber` writes exactly 96 bits: a 64-bit signed mantissa (`s.add64`) followed by a 32-bit signed exponent (`s.add32`). This contrasts with `STAmount`, which embeds asset metadata. The deserializing constructor reads the mantissa with `sit.geti64()` and the exponent with `sit.geti32()` in explicitly-separated statements (the comment documents that ordering matters for the stream iterator's side effects).

The `add()` method contains a notable defensive pattern: when `sMD_NeedsAsset` is set but no asset has been associated (i.e., `asset_` is `std::nullopt`), a debug-only assertion verifies that the active `Number::getMantissaScale()` is `MantissaRange::large`. This guards the backward-compatibility boundary — `STNumber` should only be serialized when the `SingleAssetVault` or `LendingProtocol` amendments are active, which force the large mantissa scale.

## The Underlying `Number` Type

`Number` is a floating-point representation with an internal signed 64-bit unsigned mantissa and a signed integer exponent, supporting two normalization ranges. The "small" range (mantissa in `[10^15, 10^16−1]`) matches `STAmount`'s IOU range. The "large" range (mantissa in `[10^18, 10^19−1]`) provides sufficient precision to represent all valid XRP and MPT integer values and is required by the Lending Protocol. The active range is thread-local and amendment-gated.

`STNumber` exposes the full `Number` interface implicitly through the `operator Number() const` conversion operator, allowing it to be passed directly wherever `Number` is expected. Assignment from `Number` is handled by `operator=(Number const& rhs)`, which delegates to `setValue()`. This design makes `STNumber` feel like a natural `Number` in arithmetic expressions while retaining its serialization identity.

## JSON Parsing Utilities

Two free functions support transaction submission. `partsFromString()` parses a decimal string (with optional sign, fractional part, and scientific-notation exponent) using a compiled `boost::regex` and returns a `NumberParts` struct containing a raw `uint64_t` mantissa, `int` exponent, and sign flag. `numberFromJson()` accepts an `SField` and a `Json::Value` (integer, unsigned, or string) and constructs a fully-normalized `STNumber`. It asserts that no active transaction rules are present, restricting its use to pre-transactor JSON deserialization paths where user-supplied values are parsed before ledger rules take effect.

## Non-Obvious Design Decisions

The decision to split `STAmount` into `STNumber + contextual Asset` rather than introducing a new opaque field type means that existing serialization infrastructure (field types, type IDs, `Serializer`/`SerialIter`) required only a new `STI_NUMBER` type code and a new SField metadata flag, not a new wire format mechanism. The `STTakesAsset` intermediate class is deliberately minimal — it only stores the optional asset and provides a virtual `associateAsset` — so other future field types could inherit the same pattern without pulling in `STNumber`'s rounding semantics.

The `isDefault()` check compares against `Number()` (the default-constructed zero value), which uses a sentinel exponent of `std::numeric_limits<int>::lowest()` rather than zero, ensuring that a zero-valued `STNumber` round-trips correctly through `isDefault()` without false positives.