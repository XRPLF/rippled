# `STNumber.cpp` — Serializable Precision Number for XRPL Fields

## Role in the System

`STNumber` fills a gap in the XRPL serialization type hierarchy. The existing `STAmount` bundles a numeric value together with its `Asset` (currency/issuer or MPT ID), which means every ledger field that stores an amount must also redundantly store asset information. For ledger objects like Vault, LoanBroker, and Loan — where many numeric fields all refer to the same vault asset — that duplication is wasteful. `STNumber` solves this by storing only the numeric value in a `Number` (mantissa + exponent) form, deferring asset binding to runtime. The comment in the header is precise: it is "effectively an `STAmount` sans `Asset`."

The class is part of `libxrpl`'s protocol layer and participates in the same serialization framework as all other `STBase`-derived types, with type tag `STI_NUMBER`.

## Class Hierarchy and the `STTakesAsset` Mixin

Rather than inheriting directly from `STBase`, `STNumber` inherits through `STTakesAsset`, an intermediate mixin that adds an `std::optional<Asset> asset_` member. This design separates concerns cleanly: `STTakesAsset` knows how to store an asset association without caring what the derived class does with it, while `STNumber` overrides `associateAsset()` to trigger precision rounding. The `asset_` field is explicitly runtime-only — it is never serialized to the ledger.

## Serialization Wire Format and Sequencing

On the wire, an `STNumber` is 12 bytes: a 64-bit signed mantissa followed by a 32-bit exponent, written by `add()` via `s.add64()` + `s.add32()`. Deserialization in the `SerialIter` constructor reverses this exactly:

```cpp
auto mantissa = sit.geti64();
auto exponent = sit.geti32();
value_ = Number{mantissa, exponent};
```

The comment — "We must call these methods in separate statements to guarantee their order of execution" — guards against the classic C++ evaluation order pitfall. Within a single function-call argument list, the order of argument evaluation is unspecified; forcing the calls into separate `auto` statements makes sequencing well-defined.

## Asset Association and the Two-Phase Rounding Contract

The precision of a `Number` depends on the asset it represents: XRP and MPT values must fit within integer constraints, while IOU values carry 15 significant decimal digits. The `roundToAsset()` call in `associateAsset()` performs this alignment by constructing a temporary `STAmount` from the `Asset` and `Number` pair, which drives the rounding through `STAmount`'s normalization logic.

This creates a two-phase contract enforced by assertions in `add()`:

1. **Phase 1 (`associateAsset`)**: the value is rounded to the asset's precision and stored back into `value_`.
2. **Phase 2 (`add`)**: if an `asset_` is present, `roundToAsset()` is called again on a local copy and compared against `value_` via `XRPL_ASSERT_PARTS`. The assertion verifies idempotency — that the stored value was already rounded. Any mismatch indicates that `setValue()` was called after `associateAsset()` without re-associating, a programming error that would produce incorrect ledger state.

When `asset_` is absent at serialization time, the code relaxes into a debug-only check that the global `MantissaRange` is set to `large`. The "large" scale (mantissa in `[10^18, 10^19 - 1]`, amendment-gated by SingleAssetVault / LendingProtocol) is required for correctly representing XRP and MPT integer values that exceed the 15-digit "small" IOU range. Serializing an `STNumber` without an asset and without large-scale mode active would silently truncate precision, so this guard catches configuration errors in debug builds.

## Mantissa Range Check at Serialization

Before writing bytes, `add()` asserts that the `int64_t` mantissa extracted from `Number::mantissa()` falls within `[INT64_MIN, INT64_MAX]`. This appears redundant — `Number::mantissa()` already returns `std::int64_t` — but it is necessary because the internal representation uses an *unsigned* 64-bit mantissa extended to 19 digits when `MantissaRange::large` is active. The external `mantissa()` accessor divides by 10 when the internal value exceeds the signed 63-bit maximum, so the assertion documents and enforces the precondition that the wire-format representation must always fit in a signed 64-bit field.

## JSON Parsing

`partsFromString()` uses a compiled `boost::regex` (flagged `optimize`) to parse decimal notation including optional sign, integer part, fractional part, and exponent. The regex is `static` to pay the compile cost once. Fractions are absorbed into the mantissa by concatenating the integer and fractional digit strings, then setting the exponent to the negative of the fractional digit count before applying any explicit exponent.

`numberFromJson()` dispatches on the JSON value type: integer JSON values are handled directly by reading `asInt()` / `asUInt()`, while string values go through `partsFromString()`. A notable guard in the string path asserts `!getCurrentTransactionRules()`, meaning string-based JSON parsing of `STNumber` fields is only permitted outside of transaction processing. During transaction processing, fields should arrive pre-parsed or as numeric JSON; accepting string-formatted numbers in that context would allow user-supplied text to drive parsing inside a transactor, which the XRPL protocol intentionally prevents.

## Equivalence and Default State

`isEquivalent()` uses `dynamic_cast` against the already-verified same-type target, then delegates to `Number::operator==`. `isDefault()` returns `true` for a default-constructed `Number()`, which represents zero in `Number`'s internal encoding. Both methods satisfy the virtual contract from `STBase` required by the serialization framework's field diffing and canonical-form checks.

The `copy()` and `move()` overrides call the `emplace()` helper from `STBase`, enabling placement-new into pre-allocated buffers — a performance pattern throughout the ST-type family that avoids heap allocation for short-lived serialized field copies.