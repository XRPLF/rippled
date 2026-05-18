# `STAccount.cpp` — Serialized Account Field Implementation

## Role in the System

`STAccount` is the XRPL protocol's typed container for account identifiers as they appear in ledger objects and transactions. Every field in a transaction or ledger entry that holds an account address — sender, destination, issuer — is represented as an `STAccount` instance within the serialized type (ST) framework. The file implements the concrete methods of the `STAccount` class declared in the corresponding header, wiring up construction, binary serialization, equivalence, and text conversion.

## Inheritance and Design

`STAccount` inherits from both `STBase` and `CountedObject<STAccount>`. `STBase` is the polymorphic root of all serialized field types in the XRPL codebase; it binds a value to an `SField` descriptor that encodes the field's name, type code, and encoding properties. `CountedObject<STAccount>` is a lightweight CRTP mixin that tracks live instance counts for diagnostic purposes.

The class stores the account ID as an `AccountID` — a typedef for `base_uint<160, AccountIDTag>` — a strongly-typed 160-bit integer. A notable comment in the header explains that the original implementation kept the value in an `STBlob` (a variable-length byte buffer), but since account IDs are *always* exactly 160 bits, a fixed-size `uint160` is more efficient. Crucially, the wire format was deliberately kept identical to `STBlob`: values are written as VL-encoded (variable-length prefixed) blobs via `addVL()`. This means the optimization is purely internal; on the wire and in serialized ledger data, the encoding is unchanged.

## The `default_` Flag and the Zero State

A `bool default_` member tracks whether the field has been explicitly assigned a value. Both the default constructor and the `SField`-only constructor initialize `value_` to `beast::zero` and set `default_ = true`. This mirrors `STBlob`'s notion of a zero-size default blob.

The flag drives two behaviors. In `add()`, when `default_` is true, the field serializes as a zero-length VL blob — an empty byte sequence — rather than 20 zero bytes. In `getText()`, a default field returns an empty string rather than the base58 encoding of the all-zeros pseudo-account. This matters because the all-zeros value carries special meaning in the XRPL protocol (it is used as the XRP issuer sentinel), and serializing it as an empty blob cleanly distinguishes "field not set" from "field set to the zero account."

## Construction Paths

There are four meaningful constructors, each serving a different call site:

- **`STAccount(SField const& n)`** creates a named but unset field, used when building new ledger objects before populating fields.
- **`STAccount(SField const& n, AccountID const& v)`** sets the value directly from a typed `AccountID`, clearing `default_`. This is the typical path in application code.
- **`STAccount(SField const& n, Buffer const& v)`** accepts raw bytes, as returned from parsing VL blobs. An empty buffer is accepted and leaves the field in the default state (this is the round-trip representation of a default field). A non-empty buffer must be exactly `uint160::bytes` (20 bytes); any other size causes a `Throw<std::runtime_error>("Invalid STAccount size")`. The comment acknowledges the historical question of whether throwing from a constructor is safe here, but notes that the calling context (`STVar::STVar(SerialIter&, SField const&)`) already throws, so propagation is expected.
- **`STAccount(SerialIter& sit, SField const& name)`** is the deserialization entry point. It calls `sit.getVLBuffer()` to extract the variable-length blob and delegates directly to the Buffer constructor, inheriting its size validation.

## Serialization: `add()`

```cpp
void STAccount::add(Serializer& s) const
{
    XRPL_ASSERT(getFName().isBinary(), ...);
    XRPL_ASSERT(getFName().fieldType == STI_ACCOUNT, ...);
    int const size = isDefault() ? 0 : uint160::bytes;
    s.addVL(value_.data(), size);
}
```

Two `XRPL_ASSERT` calls guard that the associated `SField` is a binary field of type `STI_ACCOUNT`. These are programmer-error checks — they fire in debug builds if `add()` is called on a field that was improperly constructed with a mismatched field descriptor. In release builds, depending on the macro's implementation, they may be no-ops.

The actual serialization writes either zero bytes (for a default account) or the 20-byte raw value, both wrapped in VL encoding by `addVL()`. This preserves full round-trip fidelity with the blob-based format.

## Equivalence vs. Comparison

`isEquivalent()` is the polymorphic comparison required by `STBase`. It first `dynamic_cast`s the argument to `STAccount const*` (returning false on type mismatch), then checks that both `default_` and `value_` agree. Two `STAccount` fields are semantically equivalent only if they share the same default state and the same 160-bit value.

The header also defines non-member `operator==` and `operator<` in several overloads. These compare only `value()`, ignoring `default_`. This is a deliberate design choice: when account IDs are used as keys (e.g., in sorted ledger entries or offer books), only the actual address matters, not whether the field was "explicitly set."

## Placement-New Support

The `copy()` and `move()` private methods delegate to `STBase::emplace()`, a helper that performs either placement-new into a caller-supplied buffer (if the object fits) or heap allocation. This is the mechanism by which `detail::STVar` — the type-erased variant that stores typed serialized fields — manages `STAccount` instances efficiently without forcing heap allocation for every field.