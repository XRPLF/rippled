# STCurrency.cpp — Serialized Currency Field

## Role in the System

`STCurrency` is the serialized-type wrapper for XRPL currency identifiers. It slots into the `STBase` polymorphic hierarchy that underpins every field inside a ledger object or transaction. The "ST" prefix is protocol-wide shorthand for "Serialized Type" — every wire-format field type (`STAmount`, `STObject`, `STArray`, and so on) inherits from `STBase` and overrides the same small set of virtual methods. `STCurrency` is the leaf node in that hierarchy responsible for holding and round-tripping a single 160-bit currency code.

## The `Currency` Type

At the heart of the class is a single private member `currency_` of type `Currency`, which is `base_uint<160, detail::CurrencyTag>`. The tag parameter is a phantom type that makes `Currency`, `NodeID`, and other 160-bit values mutually incompatible without any runtime cost — an example of the policy the XRPL codebase applies throughout `UintTypes.h`. XRP itself is represented as the all-zeroes value; `isXRP()` simply tests for `beast::zero`. This zero-means-XRP convention drives the `isDefault()` override, which returns `true` when the stored currency is XRP. In `STBase`'s semantics, "default" values are omitted during canonical serialization, so a naked XRP amount field need not carry an explicit currency code on the wire.

## Construction Paths

Three constructors cover the three entry routes:

1. **Name-only** (`STCurrency(SField const&)`) — produces a default-constructed (XRP) currency. Used when an `STObject` allocates a placeholder for a field that has not yet been populated.
2. **Deserialization** (`STCurrency(SerialIter&, SField const&)`) — reads exactly 160 bits from the stream via `sit.get160()`. No validation is performed here: the binary wire format is assumed correct, and the cost of parsing the ledger database would be prohibitive if every field value were re-checked on ingestion.
3. **Direct value** (`STCurrency(SField const&, Currency const&)`) — used programmatically when the `Currency` value is already known.

The static factory `construct()` is a thin wrapper around constructor #2, kept `private` and declared `friend` of `detail::STVar`. `STVar` is the type-erased variant the `STObject` container uses internally; it discovers `construct` through a compile-time registration mechanism that maps `STI_CURRENCY` to this factory.

## Small-Buffer Copy and Move

The `copy()` and `move()` overrides delegate to `STBase::emplace()`:

```cpp
STBase* copy(std::size_t n, void* buf) const override { return emplace(n, buf, *this); }
STBase* move(std::size_t n, void* buf) override       { return emplace(n, buf, std::move(*this)); }
```

`emplace()` checks whether `sizeof(STCurrency)` fits within the caller-supplied `n`-byte `buf`. If it does, it uses placement-new into that buffer; otherwise it falls back to a heap allocation. This is the small-buffer optimization baked into `STVar`: container elements that are small enough live inline inside the parent object, avoiding a separate allocation and pointer chase for the common case. `STCurrency` is a compact type (a 160-bit integer plus a pointer-sized `fName`), so it will almost always land in the inline buffer.

## Serialization and JSON

`add(Serializer&)` emits the 160-bit value verbatim via `s.addBitString(currency_)`. `getText()` and `getJson()` both delegate to `to_string(currency_)`, which returns `""` for zero (XRP), `"XRP"` as a displayable alias, or the three-character ISO-4217-style ticker for well-known tokens, falling back to a hex string for opaque custom currencies.

`isEquivalent()` performs a `dynamic_cast` to verify the other `STBase` is actually an `STCurrency` before comparing values. This is the standard pattern across all `STBase` subclasses — they must handle polymorphic comparison without a common strongly-typed overload.

## JSON Deserialization and Validation

The free function `currencyFromJson(SField const&, Json::Value const&)` is the only place in this file where untrusted external input is handled:

```cpp
if (!v.isString()) Throw<std::runtime_error>(...);
auto const currency = to_currency(v.asString());
if (currency == badCurrency() || currency == noCurrency()) Throw<std::runtime_error>(...);
```

Two validation gates are required rather than one because of a legacy quirk in `to_currency()`: it can return `badCurrency()` on a successful parse (the three-letter string `"XRP"` used as a token identifier, which the network deliberately prohibits to prevent confusion with native XRP). The docstring in `UintTypes.h` explicitly flags this as unfortunate legacy behavior that would be risky to change. `currencyFromJson` therefore rejects both sentinel values explicitly, giving callers a clean guarantee: if it doesn't throw, the returned `STCurrency` holds a well-formed, non-reserved currency.

The asymmetry between the two deserialization paths — binary (`SerialIter`, no validation) and JSON (strict validation) — is intentional. Binary ledger data originates from a consensus-validated stream; JSON arrives from API consumers whose input cannot be trusted.