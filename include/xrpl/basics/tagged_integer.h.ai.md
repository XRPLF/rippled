# `tagged_integer.h` — Type-Safe Integer Wrapper

## Purpose

`tagged_integer.h` defines a template class that wraps any standard integral type (`Int`) and makes it a distinct C++ type by binding it to a phantom `Tag`. Two `tagged_integer` instantiations that share the same underlying integer type but carry different tags are completely separate types: they cannot be compared, assigned to each other, or passed interchangeably. This is the phantom-type (or "strong typedef") idiom applied to integers, and its entire value proposition is catching conceptual misuse at compile time rather than at runtime.

The motivation is clear in a ledger protocol codebase where many distinct concepts are represented as bare integers — ledger sequence numbers, NFT taxon codes, transaction indices, ledger IDs. Without distinct types, a function that expects a sequence number will silently accept a taxon value. `tagged_integer` eliminates this class of bug with zero runtime cost.

## Design of the Template Parameters

`tagged_integer<Int, Tag>` takes two parameters: `Int` is the underlying integral storage type, and `Tag` is a phantom type whose sole purpose is to make each instantiation unique. `Tag` never needs to be a complete or meaningful type; in practice it is defined as an empty struct nested inside the owning class:

```cpp
struct SeqTag;
using Seq = tagged_integer<std::uint32_t, SeqTag>;

struct IdTag;
using ID = tagged_integer<std::uint32_t, IdTag>;
```

`Seq` and `ID` both wrap `uint32_t`, but the compiler treats them as wholly unrelated types. You cannot pass a `Seq` where an `ID` is expected without an explicit cast.

## Constructor Narrowing Guard

The constructor is explicitly constrained to reject widening conversions:

```cpp
template <class OtherInt,
    class = typename std::enable_if<
        std::is_integral<OtherInt>::value && sizeof(OtherInt) <= sizeof(Int)>::type>
explicit constexpr tagged_integer(OtherInt value) noexcept : m_value(value)
```

The `sizeof(OtherInt) <= sizeof(Int)` guard prevents constructing a 32-bit `tagged_integer` from a 64-bit literal, which would silently truncate the value. A `uint32_t`-backed instance can be constructed from any narrower-or-equal integral type, but not from a wider one. The test suite exercises this through `static_assert` checks: `TagUInt1` (backed by `uint32_t`) is constructible from `uint32_t` but not from `uint64_t`; `TagUInt3` (backed by `uint64_t`) accepts both.

The constructor is `explicit`, so raw integers cannot implicitly convert to a `tagged_integer` — nor can one `tagged_integer` implicitly convert to another, since there is no converting constructor between tagged types. Assignment is similarly restricted: the test verifies that `TagUInt1 = TagUInt2` does not compile even when both wrap `uint32_t`.

A `static_assert` inside the constructor body confirms that no hidden padding was introduced (`sizeof(tagged_integer) == sizeof(Int)`), preserving the layout guarantee needed for contiguous hashing.

## Operator Coverage via Boost.Operators

Rather than spelling out every binary operator, the class inherits from a chain of Boost.Operators mixins:

```
boost::totally_ordered<...,
    boost::integer_arithmetic<...,
        boost::bitwise<...,
            boost::unit_steppable<...,
                boost::shiftable<...>>>>>
```

Each mixin synthesizes additional operators from the handful explicitly defined:

- `totally_ordered` derives `!=`, `>`, `<=`, `>=` from `operator<` and `operator==`.
- `integer_arithmetic` derives binary `+`, `-`, `*`, `/`, `%` (and postfix `++`/`--`) from the compound-assignment operators and `unit_steppable`.
- `bitwise` derives binary `&`, `|`, `^` from `&=`, `|=`, `^=`.
- `shiftable` derives binary `<<`, `>>` from `<<=`, `>>=`.

The compound-assignment operators are implemented by delegating to the same operation on `m_value`, so there is no performance overhead — inlining collapses them to single instructions. Unary `~`, `+`, and `-` are defined manually since they have no Boost mixin equivalent.

The explicit cast `operator Int()` provides deliberate, opt-in access to the underlying integer, used when calling APIs or doing cross-domain conversions. Its `explicit` qualifier ensures that no accidental implicit decay to the raw type occurs.

## Hashing Integration

A specialization of `beast::is_contiguously_hashable` is provided in the `beast` namespace:

```cpp
template <class Int, class Tag, class HashAlgorithm>
struct is_contiguously_hashable<xrpl::tagged_integer<Int, Tag>, HashAlgorithm>
    : public is_contiguously_hashable<Int, HashAlgorithm>
```

This inherits hashability directly from the underlying `Int` type. If `Int` can be hashed by copying its raw bytes (i.e., its bit pattern is canonical and platform-consistent), then a `tagged_integer` wrapping it can be too. The layout guarantee from the constructor's `static_assert` makes this safe: the wrapper adds no bytes and no padding, so hashing a `tagged_integer` is identical to hashing its `Int`. This enables `tagged_integer` instances to participate in Beast's hash-based containers and message-digest pipelines without any extra boilerplate per instantiation.

## Relationship to the Broader Codebase

The type is used in at least two protocol-level headers. In `include/xrpl/protocol/nft.h`, `Taxon` is defined as `tagged_integer<std::uint32_t, TaxonTag>`, distinguishing NFT taxon codes from raw integers. In `src/test/csf/ledgers.h`, the simulated ledger harness defines both `Ledger::Seq` and `Ledger::ID` as separate `tagged_integer` specializations wrapping the same `uint32_t` storage, making it impossible to accidentally pass a ledger sequence where a ledger identity is expected. `LedgerTiming.h` documents that its template function works with both built-in integers and `tagged_integer`s for the `Seq` parameter, confirming the type is designed to be a drop-in replacement wherever integers already work.