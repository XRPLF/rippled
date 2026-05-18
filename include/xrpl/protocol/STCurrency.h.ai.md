# `STCurrency.h` — Serialized Currency Field for XRPL Protocol

`STCurrency` is a thin but essential serialization wrapper in the XRPL protocol type system. Its single job is to carry a `Currency` value — a 160-bit hash (`base_uint<160, detail::CurrencyTag>`) — inside the ledger's binary field framework. It exists because every field in a serialized XRPL object must be an `STBase` subclass; raw `Currency` values cannot appear in transaction or ledger-entry fields without being wrapped this way.

## Role in the ST Type Hierarchy

The file follows the same structural pattern as `STAccount` (wraps `AccountID`, 160-bit) and `STIssue` (wraps `Asset`). All three are `final` subclasses of `STBase` that hold a single domain value, expose `value_type` aliased to their inner type, and implement the four-method `STBase` contract: `getSType()`, `getText()`, `getJson()`, `add()`, and the two virtual predicates `isEquivalent()` and `isDefault()`.

One subtle difference from `STAccount` and `STIssue`: `STCurrency` does **not** mix in `CountedObject<STCurrency>`. This means instance counts are not tracked for this type — probably an intentional omission because `STCurrency` appears infrequently in well-formed transactions, but worth noting if you ever add diagnostic instrumentation.

## The `Currency` Underlying Type

`Currency` is defined in `UintTypes.h` as `base_uint<160, detail::CurrencyTag>`. The tag type prevents accidental mixing with `NodeID` (also 160 bits) at compile time. The all-zeros 160-bit value is XRP's canonical currency representation (`xrpCurrency()` returns `beast::zero`). Three-character ISO codes like "USD" are encoded into specific byte positions of the 160-bit field per the XRPL specification. The sentinel values `noCurrency()` and `badCurrency()` mark absent or invalid entries; `badCurrency()` encodes what looks like the string "XRP" in the ISO field, which is explicitly banned because early users confused it with native XRP.

## Construction and Deserialization

Three constructors cover all usage paths:

- `STCurrency(SField const& name)` — creates a default (XRP) currency with a field name but no data yet; used when constructing template objects.
- `STCurrency(SField const& name, Currency const& currency)` — the normal programmatic constructor.
- `STCurrency(SerialIter& sit, SField const& name)` — deserializes from a wire stream by reading exactly 160 bits via `sit.get160()`.

The private `construct(SerialIter&, SField const&)` static factory is the hook for `detail::STVar`, which dispatches object construction from a `SerializedTypeID` at runtime. `STVar` implements small-object optimization: its aligned storage holds up to 72 bytes, and the virtual `copy()`/`move()` overrides call `STBase::emplace()` to place the object in that buffer rather than heap-allocating when `sizeof(STCurrency)` fits.

## Serialization and Default Semantics

`add(Serializer& s)` writes the currency as a raw 160-bit string via `s.addBitString(currency_)`. There is no framing — just the 20-byte payload — consistent with how all fixed-width ST scalar types are encoded.

`isDefault()` returns `true` when `isXRP(currency_)`, i.e., when the 160-bit value is zero. This drives the `STBase` equality convention: the default-constructed `STCurrency{}` represents XRP, and the ledger elides fields at their default values during serialization to reduce payload size.

`getText()` and `getJson()` both delegate to `to_string(currency_)`, which returns `""` for XRP (all zeros), `"XRP"` for the XRP code, or the 3-character ISO string for IOU currencies. JSON emission intentionally ignores the `JsonOptions` flags — a currency code is always a plain string with no optional decoration.

## JSON Parsing with `currencyFromJson()`

The free function `currencyFromJson(SField const& name, Json::Value const& v)` is the input validation gateway. It enforces two invariants before constructing an `STCurrency`:

1. The JSON value must be a string (not a number or object).
2. The resulting `Currency` must not be `badCurrency()` or `noCurrency()`.

Notably, `to_currency()` has a legacy quirk documented in `UintTypes.h`: it may silently return `badCurrency()` for invalid inputs rather than failing. `currencyFromJson()` compensates by explicitly rejecting both sentinel values, acting as the defensive layer that protects the rest of the system from malformed input arriving through the API.

## Comparison Operators

The header provides two sets of comparison operators. The `STCurrency`-vs-`STCurrency` set gives `==`, `!=`, and `<`, enabling use in sorted containers and comparison chains. The mixed `STCurrency`-vs-`Currency` set provides `==` and `<` for comparing a wrapped field directly against an unwrapped value, which avoids unnecessary construction of a temporary `STCurrency` in lookup contexts. The asymmetric design — mixed comparators only go one direction (`STCurrency` on the left) — follows the same pattern as `STAccount`, keeping the operator set minimal while covering the most common use cases in the transaction engine.