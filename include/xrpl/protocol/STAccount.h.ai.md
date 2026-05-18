# `STAccount` — Serialized Account ID Type

## Role and Purpose

`STAccount` is the XRPL protocol's serialized-type wrapper around a 160-bit account identifier. It lives in the "ST" (Serialized Type) family of classes that form the building blocks of every XRPL transaction and ledger object. Its job is to carry an `AccountID` value through the protocol stack in a form that knows how to serialize itself, compare itself, and report whether it has ever been explicitly set.

## Inheritance Design

`STAccount` inherits from two bases simultaneously:

- **`STBase`** — the abstract root of all serialized types, supplying field-name association, the `Serializer` interface (`add`, `addFieldID`), the virtual dispatch contract (`getSType`, `isEquivalent`, `isDefault`, `getText`), and the small-buffer `emplace` helper used by `copy` / `move`.
- **`CountedObject<STAccount>`** — a CRTP mixin that atomically tracks the number of live `STAccount` instances. Every constructor increment and every destructor decrement goes through a lock-free linked list of counters, giving the node operator diagnostic visibility into object lifetimes without any per-instance overhead beyond what a vtable already costs.

The `final` specifier signals that no further derivation is expected.

## Storage Optimization and Serialization Compatibility

The most architecturally interesting aspect of `STAccount` is captured in a comment at the top of the private section:

> *The original implementation kept the value in an `STBlob`. But an `STAccount` is always 160 bits, so we can store it with less overhead in an `xrpl::uint160`. However, so the serialized format stays unchanged, we serialize and deserialize like an `STBlob`.*

So the storage is an `AccountID` (a `base_uint<160>`) — a fixed-size, stack-friendly type with no heap allocation — while the wire format is the variable-length (VL-prefixed) blob encoding that `STBlob` uses. The `add()` method preserves this contract by calling `s.addVL()`, and the `Buffer`-accepting constructor plus the `SerialIter` constructor both parse the incoming bytes as a VL blob before copying the raw bits into the `uint160`. This avoids breaking any existing node on the network while eliminating the internal `std::vector` allocation that `STBlob` would carry.

## The `default_` Flag

Alongside `value_`, a `bool default_` tracks whether the account has been explicitly set. This is not merely `value_ == beast::zero`: the zero account (`noAccount()`) is a legal, meaningful XRPL value. `default_` instead encodes *was this field ever assigned*, which drives two behaviors:

1. **Serialization**: `add()` serializes a default account as a zero-length VL blob, matching the STBlob convention for absent fields, rather than writing 20 bytes of zeros that might be misinterpreted as an explicitly-supplied zero account.
2. **Equivalence**: `isEquivalent()` checks both `default_` and `value_`, so two `STAccount` objects are equivalent only if their "set-ness" and their binary content match.

`setValue()` (and the assignment operator that delegates to it) unconditionally clears `default_` to `false`, so any explicit assignment — even to the zero account — marks the field as having been set.

## Constructors

Four public constructors cover the main use cases:

- `STAccount()` — default-constructs to zero / unset; used when building an empty STObject slot.
- `STAccount(SField const&)` — names the field but leaves it unset; the SField is mandatory metadata that identifies which account field this is in a transaction or ledger entry.
- `STAccount(SField const&, Buffer const&)` — deserialization path from raw bytes; validates the buffer is exactly 20 bytes and throws `std::runtime_error` otherwise; an empty buffer is accepted and leaves the field in default state.
- `STAccount(SerialIter&, SField const&)` — the primary deserialization constructor, delegates to the `Buffer` form by extracting a VL blob from the iterator.
- `STAccount(SField const&, AccountID const&)` — direct construction from a known account ID, immediately marking the field as set.

## Operators and Comparisons

The file exposes a full set of free comparison operators that allow `STAccount` and raw `AccountID` values to be compared interchangeably, covering `operator==` and `operator<` in both orderings (e.g. `AccountID < STAccount`). These are all thin wrappers around the `uint160` comparison operations and are defined `inline` in the header to allow the compiler to optimize them away at call sites. There is no `operator!=` because callers compose it from `!operator==` via standard convention.

## Private `copy` / `move` Hooks

`STBase::emplace()` implements a small-buffer optimization: if the object fits within `n` bytes of a caller-supplied stack buffer, it is placement-new'd there; otherwise it falls back to heap allocation. `STAccount` overrides `copy` and `move` to delegate to this mechanism, enabling `detail::STVar` — the discriminated union used inside `STObject` field slots — to embed small ST types on the stack rather than heap-allocating them, which is why `detail::STVar` is declared a `friend`.