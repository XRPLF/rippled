# `xrpl/basics/Slice.h` — Immutable Byte-Range View

`Slice` is the foundational non-owning byte-view type for the XRPL codebase. It fills the same conceptual role as `std::string_view` or `std::span<const std::uint8_t>` but predates both, and is tightly integrated with XRPL's binary protocol handling infrastructure.

## Core Design

The class holds exactly two fields: a `const uint8_t*` pointer and a `size_t` length. This makes it trivially copyable — two words on any 64-bit platform — and safe to pass by value everywhere, which is the intended usage. A default-constructed `Slice` is always valid, representing an empty range. There is no null-vs-empty distinction: an empty `Slice` is simply one with `size_ == 0`, and a null pointer is only possible when size is also zero.

The class is explicitly `const`-only. There is no non-const `data()` accessor and no mutable iterator. This is deliberate: when a subsystem needs to mutate bytes, it uses `Buffer` (the companion owning type in `Buffer.h`). `Slice` is the read-only transport token.

## Advancing and Trimming

Two families of mutators allow consuming a byte stream in-place. `operator+=` / `operator+` advance the start of the slice and throw `std::domain_error` (via the `Throw<>` contract helper from `contract.h`) if `n > size_`. This makes them appropriate for protocol parsing loops where overrun is an error condition. In contrast, `remove_prefix` and `remove_suffix` perform no bounds checking, mirroring the intentionally unchecked semantics of `std::string_view::remove_prefix`. Callers are responsible for validating bounds before calling these. The distinction is important: the `+=` operator is for defensive parsing code, while `remove_prefix`/`remove_suffix` are for tight paths where invariants are already established.

`substr()` follows `std::string_view::substr` semantics exactly: it throws `std::out_of_range` if `pos > size()`, but clamps `count` to avoid running past the end. This means `substr(pos)` reliably returns the tail of a slice without requiring the caller to compute the remaining length.

## Indexing and Hashing

`operator[]` is guarded only by `XRPL_ASSERT`, which is a debug-mode check via the Beast instrumentation layer. This trades safety in release builds for performance in hot paths, consistent with how the rest of the protocol code handles per-byte access. Callers are expected to validate bounds externally.

The `hash_append` template function integrates `Slice` with XRPL's open hashing protocol. Any hasher that satisfies the `Hasher` concept (takes a pointer and byte-count) can hash a `Slice` directly. This is how `base_uint` instances, serialized ledger objects, and other byte sequences are hashed for use in unordered containers and the SHAMap.

## Stream Output and Comparisons

`operator<<` renders a `Slice` as its uppercase hex representation via `strHex`, which in turn uses `boost::algorithm::hex`. This hex rendering appears throughout XRPL logging and diagnostic output. The equality operator uses `std::memcmp` (fast, no locale concerns) and the less-than operator uses `std::lexicographical_compare`, giving `Slice` a total order suitable for use in sorted containers.

## The `makeSlice` Factory Functions

The three `makeSlice` overloads provide safe, implicit-free construction from common standard containers. The array and vector overloads are constrained via `std::enable_if` to only accept `T = char` or `T = unsigned char`, preventing the accidental construction of a `Slice` from a `std::vector<int>` or similar. The `std::basic_string<char>` overload has no such constraint since `char` is always the element type. These factories centralize the `reinterpret_cast` that is otherwise unavoidable when going from `char*` to `uint8_t*`, keeping that unsafe operation in one place.

## Relationship to `Buffer`

`Buffer` is the owning counterpart: it manages a heap-allocated `unique_ptr<uint8_t[]>` and provides an implicit conversion `operator Slice()`. The explicit constructor `Buffer(Slice)` deep-copies from a slice. The assignment `Buffer& operator=(Slice)` includes an XRPL_ASSERT guard to detect the case where the source slice is a subset of the buffer being overwritten — a subtle aliasing bug that would otherwise silently corrupt data. Together, `Slice` (non-owning, cheap, immutable) and `Buffer` (owning, allocated, mutable) form the complete binary data vocabulary used throughout the XRPL protocol layer, including `Serializer`, `STBlob`, `SHAMapItem`, cryptographic key types, and the conditions/fulfillments subsystem.