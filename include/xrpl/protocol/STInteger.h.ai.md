# `STInteger.h` — Serialized Integer Types for XRPL Protocol Fields

## Role in the System

`STInteger<Integer>` is the template class that wraps plain C++ integer values inside the XRPL "Serialized Type" (ST) framework. Every integer-valued field in a ledger entry, transaction, or metadata object — sequence numbers, flags, fees, ledger indices, timestamps, transaction types — is represented at runtime as one of the five concrete instantiations defined at the bottom of this file:

```cpp
using STUInt8  = STInteger<unsigned char>;
using STUInt16 = STInteger<std::uint16_t>;
using STUInt32 = STInteger<std::uint32_t>;
using STUInt64 = STInteger<std::uint64_t>;
using STInt32  = STInteger<std::int32_t>;
```

These aliases are the types that `STObject`'s `getFieldU32()`, `setFieldU32()`, `getFieldU64()`, `getFieldI32()`, etc., read and write through.

## Inheritance Design

`STInteger<Integer>` inherits from two bases. `STBase` provides the field-name binding (an `SField const*`), the virtual serialization interface (`add()`, `getJson()`, `getText()`), and the `emplace()` helper. `CountedObject<STInteger<Integer>>` adds lock-free per-template-instantiation instance counting, maintaining a global linked list of live objects for diagnostics. Each template instantiation gets its own atomic counter, so the diagnostic system can report live `STUInt32` and `STUInt64` counts separately.

## Split Between Header and Implementation

The class declares all member functions in the header, but the implementations of `getSType()`, `getText()`, `getJson()`, and the deserialization constructor (`SerialIter&, SField const&`) are explicit full specializations in `STInteger.cpp`. The generic versions of `add()`, `isDefault()`, `isEquivalent()`, `operator=`, `value()`, `setValue()`, and `operator Integer()` are short enough to inline and live directly in the header.

This split is intentional: the per-type specializations carry semantic knowledge that doesn't belong in a generic header. `STUInt8::getText()` checks whether the field name is `sfTransactionResult` and, if so, converts the raw byte through `transResultInfo()` to a human-readable TER string. `STUInt16::getJson()` maps `sfLedgerEntryType` and `sfTransactionType` values to their format-name strings via `LedgerFormats::getInstance()` and `TxFormats::getInstance()`. `STUInt32::getText()` and `getJson()` handle `sfPermissionValue` lookups. These concerns would drag heavy dependencies (`TER.h`, `LedgerFormats.h`, `TxFormats.h`, `Permissions.h`) into every translation unit that includes `STInteger.h` — the split keeps those out of the header entirely.

`STUInt64::getJson()` has a subtler concern: JSON does not safely represent 64-bit integers in all environments. By default the method emits the value as a hex string; if the field carries the `SField::sMD_BaseTen` metadata flag it switches to a decimal string. Either way, the value is always a string in JSON, never a bare number.

## Serialization and Invariant Assertions

`add()` writes the raw integer into a `Serializer` using `s.addInteger(value_)`. Two `XRPL_ASSERT` guards fire in debug builds: one checks that the field is marked binary (no text-only fields should reach the serializer path), and one verifies that the field's declared `fieldType` matches the `getSType()` return for this instantiation. These assertions catch mis-wiring of field definitions early.

`isDefault()` returns `value_ == 0`. The ST framework uses this to omit default-valued fields from serialized output, keeping wire representations canonical and compact.

`isEquivalent()` performs a `dynamic_cast` to verify the runtime type matches before comparing values. This guards against comparing an `STUInt32` with an `STUInt64` that happen to hold the same bit pattern — a subtle correctness issue that a plain value compare would miss.

## Small-Buffer Optimization via `emplace()`

The private `copy()` and `move()` overrides delegate to `STBase::emplace()`:

```cpp
STBase* copy(std::size_t n, void* buf) const override { return emplace(n, buf, *this); }
STBase* move(std::size_t n, void* buf)       override { return emplace(n, buf, std::move(*this)); }
```

`emplace()` checks whether `sizeof(STInteger<Integer>)` fits within `n` bytes; if it does, it placement-new's the object into the caller-supplied buffer rather than heap-allocating. This is the mechanism `detail::STVar` — the discriminated-union variant that stores polymorphic ST objects inside `STObject` — uses to avoid a heap allocation for small types. `STVar` is granted `friend` access so it can call these private methods directly.

## Value Access

Three orthogonal access points are provided for the wrapped value: `value() const noexcept` for explicit read, `operator Integer() const` for implicit conversion in arithmetic or comparison contexts, `operator=(value_type const&)` and `setValue(Integer)` for mutation. Having both an explicit accessor and an implicit conversion is intentional: code that explicitly wants a raw integer calls `.value()` for clarity, while code working with the ST object in a context that expects `Integer` (e.g., passing to a function taking `std::uint32_t`) benefits from the implicit conversion without a verbose cast.