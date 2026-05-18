# `include/xrpl/beast/hash/hash_append.h`

## Role in the System

This header is the foundation of the XRPL `beast` hashing framework. It implements the "hash append" idiom popularized by Howard Hinnant's N3980 proposal — a composable, algorithm-agnostic mechanism for feeding structured C++ data into any hash algorithm. Rather than tying types to a specific hash function or to `std::hash`, the pattern separates *what gets hashed* (the type) from *how it is hashed* (the algorithm), allowing both to evolve independently. Everything in the `beast::uhash` and `xrpl::hardened_hash` pipeline flows through the overloads defined here.

## The Hasher Concept

The framework is parameterized on a `Hasher` type that must satisfy three requirements: a callable `operator()(void const*, std::size_t)` that feeds raw bytes into the hasher state, a `static constexpr boost::endian::order endian` member declaring the byte order the hasher expects, and an `explicit operator result_type()` conversion to extract the final digest. The concrete implementation, `beast::xxhasher`, sets `endian == boost::endian::order::native`, meaning it accepts data in whatever byte order the host CPU uses and performs no byte swapping. A hypothetical network-canonical hasher could instead declare `endian == boost::endian::order::big`, triggering automatic byte reversal on little-endian platforms.

## Two-Tier Type Classification

The file establishes two layered metafunctions that control how data is fed to the hasher.

`is_uniquely_represented<T>` asks: *can equal values always be compared by raw `memcmp`?* Integers, enums, and pointers qualify because their in-memory bit patterns are in bijection with their abstract values. Floating-point types deliberately do not qualify — IEEE 754 defines `+0.0 == -0.0` yet the two have distinct bit patterns. Composites (`std::pair`, `std::tuple`, `std::array`, C arrays) qualify only when all members are uniquely represented *and* the compiler has added no padding (verified via `sizeof` comparisons).

`is_contiguously_hashable<T, HashAlgorithm>` refines this: a type is contiguously hashable if it is uniquely represented *and* either the element size is one byte (byte order is irrelevant for single bytes) or the hasher's declared endian matches the native byte order. When this predicate is true, the entire object or range can be passed to the hasher as a single `h(ptr, size)` call. When false, each element must be individually dispatched through `hash_append`, where the endian machinery can intervene.

## Endian-Safe Scalar Hashing

The `detail` namespace provides `reverse_bytes(t)`, which in-place swaps all bytes of a scalar, and `maybe_reverse_bytes(t, hasher)`, which dispatches through a `std::integral_constant<bool, ...>` tag to conditionally invoke the reversal. The condition evaluates at compile time: `Hasher::endian != boost::endian::order::native`. Because `xxhasher` always uses native order, these functions are no-ops in practice, but the machinery is there for hasher implementations that demand a fixed byte order across platforms.

The three scalar `hash_append` overloads reflect the classification logic precisely:
- Contiguously hashable types (integers/enums/pointers on native-endian hashers): passed directly via `h(addressof(t), sizeof(t))`.
- Non-contiguously-hashable integrals, enums, and pointers: copied to a local, byte-reversed, then passed.
- Floating-point values: normalized by the idiom `if (t == 0) t = 0` — this collapses `-0.0` to `+0.0` before hashing, enforcing the invariant that equal values hash identically.

## Container Overloads and Length Disambiguation

Every container overload appends the container's *element count* after the element data. This is not redundant — without the size, a `vector<int>` containing `{1, 2}` and another containing `{1}` followed immediately by `{2}` would produce identical byte sequences when their data is concatenated. Appending the size creates a domain separator that makes length-extension ambiguity impossible.

For containers whose elements are contiguously hashable, the data is flushed to the hasher in one bulk call (`h(v.data(), v.size() * sizeof(T))`), trading the element-by-element dispatch overhead for a single operation. For other containers the loop approach is used.

A subtle issue exists in the `boost::container::flat_set` contiguous-hashable path, which reads `h(&(v.begin()), v.size() * sizeof(Key))`. This takes the address of the *iterator object* rather than the address of the first element. The correct expression would be `h(&(*v.begin()), ...)` or `h(v.data(), ...)`. The non-contiguous path (element-by-element loop) is unaffected.

## Tuple Hashing Trick

Tuples that are not contiguously hashable use a pre-`constexpr for` idiom. `detail::tuple_hash` expands `hash_one(h, std::get<I>(t))...` inside the argument list of `for_each_item(...)`, which accepts `...` and does nothing. Each `hash_one` call returns `int` to give the pack expansion something to hold, and `for_each_item` swallows the pack. This ensures all tuple elements are hashed in index order without requiring fold expressions or recursive templates.

## Notable Special Cases

`hash_append` for `std::shared_ptr<T>` hashes the raw pointer address (`p.get()`), not the pointed-to value. This treats shared ownership groups as distinct hash keys by identity, which is appropriate when the pointer itself is the key.

`hash_append` for `std::error_code` hashes both `ec.value()` (the numeric code) and `&ec.category()` (the address of the singleton category object). This correctly distinguishes identical numeric codes from different error domains, which `ec.value()` alone would conflate.

## ADL Extension Mechanism

The real power of the pattern is extensibility. User-defined types extend it by defining a free `hash_append(Hasher&, MyType const&)` function in the same namespace as `MyType`. Argument-dependent lookup then finds the right overload without any registration or inheritance. The XRPL codebase uses this pervasively: `base_uint` bypasses endian logic entirely (`h(a.data_.data(), sizeof(a.data_))` — its big-endian binary representation is part of the wire protocol), and `Slice` reduces to a single raw-byte flush since it already represents a plain byte range. Both declare their own `hash_append` overloads that live next to the type definition and are pulled in automatically by ADL wherever `hash_append` is called in a template context.

## Relationship to `uhash` and `hardened_hash`

`beast::uhash<Hasher>` is a one-liner adapter that constructs a `Hasher`, calls `hash_append(h, t)`, and returns `static_cast<result_type>(h)`. It is the `std::hash`-compatible entry point for unordered containers.

`xrpl::hardened_hash<HashAlgorithm>` extends this with per-instance random seeds generated at construction time (using `std::random_device` → `std::mt19937_64`). The seeds are passed to the hasher constructor, mixing them into every digest. This prevents hash-flooding denial-of-service attacks where an adversary submits keys crafted to maximise collisions in an unordered container — because the seed is unpredictable, the adversary cannot predict which inputs will collide.