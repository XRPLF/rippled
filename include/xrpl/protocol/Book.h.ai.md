# `include/xrpl/protocol/Book.h`

## Role in the System

`Book.h` defines the `Book` type, the fundamental unit of XRPL's decentralized exchange (DEX). An order book on the XRPL is an ordered set of offers to exchange one asset for another; `Book` encodes the identity of that book as a directed pair of `Asset` values — `in` (the asset being spent by a taker) and `out` (the asset being received). Every offer on the ledger belongs to exactly one such book, and ledger indexes, subscription filters, and offer-matching traversal all navigate books by this identity.

The file also consolidates `std::hash` and `boost::hash` specializations for the three related protocol types — `Issue`, `MPTIssue`, and `Asset` — that are needed anywhere `Book` or its component types are stored in unordered containers. Placing all of these hash specializations here avoids scattered partial-template-specialization definitions across translation units.

## The `Book` Class

```cpp
class Book final : public CountedObject<Book>
{
public:
    Asset in;
    Asset out;
    std::optional<uint256> domain;
};
```

`Asset` is itself a `std::variant<Issue, MPTIssue>`, so a `Book` can represent any combination of the three asset classes the ledger supports: native XRP (via `Issue`), IOU tokens (via `Issue`), and Multi-Purpose Token issuances (via `MPTIssue`). This generalization was introduced when MPTs were added to the protocol; the original design only supported `Issue` pairs.

The optional `domain` field represents a permissioned DEX domain — an on-ledger object (`PermissionedDomain`) that gates which accounts may participate in the book. When `domain` is set, the book is scoped to that domain's `uint256` ledger index, yielding an isolated order book namespace. Books with and without a domain hash and compare differently even if their `in`/`out` assets are identical, so the domain participates in equality, ordering, and hashing at every level.

Inheriting from `CountedObject<Book>` is purely observational: the CRTP base maintains a global atomic counter of live `Book` instances, accessible through `CountedObjects::getCounts()`. This is used for diagnostic reporting under load, not for any protocol logic.

## Consistency and Direction

`isConsistent(book)` (implemented in `Book.cpp`) enforces two invariants: both component assets must individually be consistent (non-bad currency, valid MPT issuer), and `book.in != book.out`. A book where both legs name the same asset is meaningless and would cause infinite-loop offer matching.

`reversed(book)` returns a new `Book` with `in` and `out` swapped while preserving `domain`. This is used when a subscription or traversal needs to walk the complementary book — for example, to find all offers that implicitly cross a given book by approaching from the other direction.

`to_string(book)` formats as `"in->out"` using the underlying `to_string(Asset)` helpers, making log output and error messages readable.

## Ordering and Equality

`Book` provides both `operator==` and `operator<=>` (three-way comparison), enabling use in sorted containers such as `std::map` and `std::set`. The three-way comparison first orders by `in`, then by `out`, then by `domain`. The manual `std::optional` comparison in `operator<=>` treats an absent domain as less than any present domain. This ordering is important because the ledger's `BookDirs` traversal iterates over offer directories sorted by these keys, and off-ledger structures like subscription routing tables depend on deterministic ordering.

The `std::optional` case is handled manually rather than relying on the standard library's built-in spaceship support for `optional` because the code needs explicit `std::weak_ordering` return type consistency with the `Asset` spaceship result, and to be safe against library versions where optional comparison behavior varies.

## Hash Infrastructure

The header provides four `std::hash` specializations (with corresponding `boost::hash` wrappers that simply inherit from them):

- `std::hash<Issue>` — combines currency hash with account hash, short-circuiting account for XRP (where all issuers are equivalent).
- `std::hash<MPTIssue>` — hashes only the 192-bit `MPTID`.
- `std::hash<Asset>` — visits the variant and dispatches to the appropriate specialization.
- `std::hash<Book>` — hashes `in`, then `boost::hash_combine`s `out`, then conditionally combines `domain` if present.

The pattern of `boost::hash<T>` inheriting `std::hash<T>` avoids code duplication while satisfying Boost.Unordered and Boost.MultiIndex containers that look up `boost::hash` rather than `std::hash`. The old comment `// VFALCO NOTE broken in vs2012` alongside disabled `using Base::Base` constructor inheritance is a legacy relic — Visual Studio 2012 did not support inheriting constructors, so explicit defaulted constructors are provided instead.

The `hash_append` template function (used with beast's cryptographic hashing pipeline) is defined inline in the header and explicitly excludes `domain` from hashing only when absent, so the same `Book` with and without a domain produces different cryptographic fingerprints. This matters for ledger index derivation in `Indexes.cpp`, where `book.domain`'s presence conditionally changes the hash inputs used to locate the book's offer directory.