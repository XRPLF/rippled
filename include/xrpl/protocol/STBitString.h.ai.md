# `STBitString.h` — Fixed-Width Bit String Serialized Type

## Role and Purpose

`STBitString<Bits>` is the XRPL serialization layer's representation of fixed-width opaque bit arrays. It bridges the raw `base_uint<Bits>` arithmetic type — which holds values like transaction hashes, account IDs, and ledger indices — and the protocol's typed serialization framework built on `STBase`. Every field that travels over the wire or appears in a ledger object as a 128-, 160-, 192-, or 256-bit blob is handled through one of the four aliases this file exposes: `STUInt128`, `STUInt160`, `STUInt192`, and `STUInt256`.

The name "bit string" rather than "integer" is deliberate: these values are not treated arithmetically by the serialization layer. They are opaque sequences of bits that happen to be compared and copied as units. The underlying `base_uint<Bits>` does support arithmetic, but `STBitString` itself exposes only identity (`isEquivalent`), serialization (`add`), and value access — nothing numeric.

## Template Design and the GDB Workaround

The template parameter is declared `int Bits` rather than the more natural `unsigned int Bits`. A comment in the header explains why: GDB 12.1 (and earlier) cannot locate RTTI information for templates instantiated over unsigned types, which breaks the GDB pretty-printer infrastructure used during development. The `static_assert(Bits > 0)` enforces the semantically obvious constraint that a negative or zero bit-count is meaningless, compensating for the signed type allowing values that `unsigned` would have rejected outright at the type-system level.

## Inheritance: STBase and CountedObject

`STBitString<Bits>` inherits from two bases. `STBase` provides the named-field abstraction: every serialized object carries an `SField` identifier that ties a value to a canonical field name, wire-type code, and binary codec behavior. This means an `STUInt256` isn't just a 256-bit value in memory — it knows whether it's a `LedgerIndex`, a `TransactionID`, a `ParentHash`, and so on. The `getSType()` method is specialized (not overridden via template) for each of the four concrete bit widths, returning the distinct wire-type codes `STI_UINT128`, `STI_UINT160`, `STI_UINT192`, and `STI_UINT256` registered in `SField.h`.

`CountedObject<STBitString<Bits>>` is a lightweight CRTP mixin that maintains a global atomic instance counter per instantiation. This is purely diagnostic: the server can report live object counts by type, which helps track down memory leaks or unexpected retention of large STObject trees.

## Constructors and Deserialization

Four constructors cover the full lifecycle:

- `STBitString(SField const& n)` — named field with a zero-initialized value, used when building objects programmatically before the value is known.
- `STBitString(value_type const& v)` — anonymous value, typically used in temporary computations where field identity doesn't matter.
- `STBitString(SField const& n, value_type const& v)` — the common case: a fully specified named field.
- `STBitString(SerialIter& sit, SField const& name)` — deserialization constructor; delegates to the third constructor by calling `sit.getBitString<Bits>()`, which reads exactly `Bits/8` bytes from the incoming stream at the current cursor position. This keeps deserialization logic centralized in `SerialIter` rather than scattered across each type.

## Serialization: the `add` Method

`add(Serializer& s)` writes the value into a byte buffer for wire encoding. Two `XRPL_ASSERT` calls guard the precondition that the associated `SField` must be of binary type and must match the object's own `getSType()`. These checks exist because `add` is called polymorphically through the `STBase` interface — the field and type metadata must stay consistent with the actual concrete type, and detecting mismatches early prevents silent protocol corruption.

## Value Mutability and the Tag Parameter

`setValue` accepts `base_uint<Bits, Tag>` where `Tag` is a free template parameter. `base_uint` supports a tag type for its second parameter that distinguishes semantically different values of the same bit width (e.g., an `AccountID` and a raw `uint160` are both 160-bit but have different tags). The member function template accepts any tag, allowing cross-tag assignment when the caller explicitly intends it while making accidental mixing visible in code review. The implicit conversion operator `operator value_type()` and the `value()` accessor both return the tag-free `value_type`, erasing tag information on the way out.

## Copy/Move and STVar Integration

The private `copy` and `move` overrides implement `STBase`'s small-buffer optimization protocol used by `detail::STVar`. `emplace` (inherited from `STBase`) placement-constructs the object into a caller-supplied buffer if it fits within `n` bytes; otherwise it heap-allocates. This avoids dynamic allocation for small serialized types that are frequently embedded in `STObject` containers. `STVar` is declared a `friend` so it can call these private methods directly.

## Defaultness

`isDefault()` returns `true` when the stored value equals `beast::zero`, which is a sentinel expression for the all-zeros bitstring. This is used by the serialization layer to decide whether a field can be omitted when encoding — default-valued fields typically do not need to be serialized in canonical binary form.