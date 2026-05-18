# `STBase.h` — Root of the XRPL Serialized Type Hierarchy

## Purpose and Role

`STBase` is the abstract base class for every serialized type ("ST") in the XRPL protocol layer. Every field that appears in a transaction, ledger entry, or validation — integers, amounts, account IDs, blobs, arrays, nested objects — is represented as a class that inherits from `STBase`. The header also defines `JsonOptions`, the bitmask type governing how those types render to JSON.

The abbreviation "ST" means "Serialized Type." Understanding `STBase` is a prerequisite for understanding the entire protocol object model: `STInteger`, `STAmount`, `STBlob`, `STObject`, `STArray`, `STTx`, `STLedgerEntry`, and every other protocol field type all derive from it.

## The Field Identity Invariant

Each `STBase` instance carries a single private member: `SField const* fName`. An `SField` is an immutable global descriptor that binds together a human-readable field name, a numeric type code (`SerializedTypeID`), and a numeric field code. This pairing is what allows the binary wire format to tag every field unambiguously. `setFName()` and `getFName()` expose this, and `addFieldID()` writes the combined type+field prefix byte(s) to a `Serializer` as the first step of encoding any field.

## The Virtual Interface

Subclasses override a focused set of virtual methods:

- `getSType()` — returns the `SerializedTypeID` enum value identifying this concrete type.
- `add(Serializer&)` — the primary serialization method: writes the field's binary payload into the given `Serializer` byte stream (the field ID header is written separately via `addFieldID()`).
- `isEquivalent(STBase const&)` — value equality ignoring field names. Used by `detail::STVar`'s `operator==`.
- `isDefault()` — returns `true` when the field holds its default value (zero for integers, etc.), enabling omission of optional fields during serialization.
- `getJson(JsonOptions)` — renders to a `Json::Value` with options controlling version and date inclusion.
- `getText()` / `getFullText()` — human-readable string forms; `getFullText()` typically prepends the field name.

## The Intentionally Asymmetric Copy Assignment

The comment at line 88 is critical and easily missed: `STBase::operator=` deliberately does **not** copy the field name from the source. It copies only the value. The rationale is that `STObject` (which stores fields in a `std::vector<detail::STVar>`) uses copy assignment to "slide down" remaining elements when a field is deleted. If copy assignment also moved the field name, an element at position *i* would acquire the name of its neighbour after every deletion. The current design keeps value-copy and identity-copy as separate operations, at the cost of making `STBase`-derived objects unsafe to store directly in standard containers — hence the prominent warning to use `boost::ptr_*` containers instead.

## `downcast<D>()`: Safe Narrowing

Rather than exposing raw `dynamic_cast` at call sites, `STBase` provides the `downcast<D>()` template. It calls `dynamic_cast<D*>(this)` and throws `std::bad_cast` (via the project-uniform `Throw<>` helper) if the cast fails. This centralizes the failure path and makes accidental silent null-pointer dereferences impossible. Concrete code that retrieves a field from an `STObject` and needs to treat it as, say, an `STAmount` uses this method.

## `emplace()` and the Small-Object Optimization

The protected static `emplace(n, buf, val)` template is the bridge between `STBase` and `detail::STVar`. `STVar` is a variant-like wrapper (with a 72-byte inline storage buffer) that owns an ST object. To avoid heap allocations for small types, `STVar` calls the virtual `copy()` or `move()` on an existing `STBase`, passing its inline buffer and its size as arguments. Every concrete subclass implements `copy()` and `move()` identically:

```cpp
STBase* copy(std::size_t n, void* buf) const override {
    return emplace(n, buf, *this);
}
```

`emplace` decides: if `sizeof(U) > n`, heap-allocate with `new U(...)`; otherwise, placement-new into `buf`. The `copy()`/`move()` virtuals are private and only accessible to `detail::STVar` (declared `friend`), enforcing that this low-level placement mechanism is never called from generic code.

## `JsonOptions` Bitmask

`JsonOptions` is a small strongly-typed bitmask wrapper around `unsigned int`. Its two meaningful bits control whether a JSON rendering includes a date field (`include_date`) and whether legacy pre-API-v2 formatting is suppressed (`disable_API_prior_V2`). The `operator~` is intentionally bounded by the `_all` mask so that bitwise complement doesn't set reserved or future bits. The free `to_json()` function template at namespace scope provides a uniform ADL-accessible entry point for any type that exposes `getJson(JsonOptions)`.

## Relationship to `detail::STVar` and `STObject`

`STBase` friends only `detail::STVar`. This single friendship grants `STVar` access to `copy()` and `move()`, which are otherwise private. `STObject` stores its fields as a `std::vector<detail::STVar>`, not a vector of raw `STBase` pointers — this is precisely the safe container pattern the warning at line 88 recommends. `STVar` manages object lifetime, handles the inline-vs-heap allocation decision, and presents a clean `STBase&` interface to callers. The combination of `STBase::emplace()`, the private virtuals, and the `STVar` friendship is the mechanism that makes type-erased, allocation-optimized storage of heterogeneous ST objects possible without a separate allocator or external memory pool.