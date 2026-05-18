# `uhash.h` — Universal Hash Functor

`uhash.h` defines the `beast::uhash` template struct, a thin adapter that bridges two complementary systems — the `hash_append` extensibility protocol and the `xxhasher` algorithm — into a single callable compatible with the `Hash` concept required by `std::unordered_map`, `std::unordered_set`, and their variants.

## Role in the System

The file is deliberately minimal. At 27 lines, it exists purely as connective tissue. Its job is to expose a single `operator()(T const&)` that any standard hash container can call, while delegating the two hard problems elsewhere: *how to serialize a type into bytes* (`hash_append`) and *how to turn bytes into a hash value* (`xxhasher`). This separation is the design's central insight, and `uhash` is the glue that makes it usable from the container side.

In `UnorderedContainers.h`, `beast::uhash<>` appears as the default `Hash` template argument for the `xrpl::hash_map`, `hash_set`, `hash_multimap`, and `hash_multiset` type aliases. These are the non-cryptographic, performance-oriented containers used throughout the XRPL codebase wherever hash-based lookup is needed and DoS resistance is not required. The hardened variants (`hardened_hash_map`, etc.) use `hardened_hash<xxhasher>` instead, which wraps the same `xxhasher` but seeds it with a random value per-process to defeat hash-flooding attacks.

## The Template Design

```cpp
template <class Hasher = xxhasher>
struct uhash
{
    using result_type = typename Hasher::result_type;

    template <class T>
    result_type operator()(T const& t) const noexcept
    {
        Hasher h;
        hash_append(h, t);
        return static_cast<result_type>(h);
    }
};
```

Three things happen inside `operator()`: a fresh `Hasher` is default-constructed, `hash_append` is called to serialize `t` into the hasher's internal buffer, and then the hasher is cast to `result_type` to extract the final digest.

The `Hasher` is constructed fresh for each call rather than being stored as member state. This is correct because `xxhasher` tracks accumulated input; reusing a partially-consumed hasher across calls would corrupt subsequent hashes. The stateless design of `uhash` itself makes it trivially copyable and safe for concurrent read use with separate `operator()` invocations.

The `noexcept` specification on `operator()` propagates the contract established by all `hash_append` overloads and `xxhasher::operator()`. Since these are explicitly `noexcept`, the entire hash computation is guaranteed not to throw, which matters for use in container internals.

## The `hash_append` Protocol

`hash_append` is an ADL-based extension point defined in `hash_append.h`. It provides overloads for all common standard types — scalars, `std::string`, `std::vector`, `std::array`, `std::pair`, `std::tuple`, `std::shared_ptr`, chrono types, `boost::container::flat_set`, and more. Types that are "uniquely represented" (no padding, no alternative bit patterns for equal values) can be hashed by a single `memcpy`-style call to the hasher. Types that aren't (e.g., IEEE floats, types with padding) are walked element by element or byte-normalized first.

Critically, the protocol handles endianness. `xxhasher` declares `endian = boost::endian::order::native`, which means `hash_append` skips byte-swapping for multi-byte scalars on the current platform. This optimizes the common case while keeping the protocol correct on big-endian platforms or with hypothetical cross-endian hashers.

Custom types integrate by providing a free function `hash_append(Hasher&, MyType const&)` in their own namespace; ADL ensures `uhash` finds it automatically without any modification to `uhash` or `hash_append.h` itself.

## The Default Hasher: `xxhasher`

`xxhasher` wraps the XXH3 64-bit algorithm from the xxHash library. It maintains a 64-byte aligned internal buffer to avoid the overhead of the streaming API for small inputs — most ledger object hashes will fit entirely in that buffer, making them single-call `XXH3_64bits()` invocations rather than streaming updates. The streaming `XXH3_state_t` path is only activated when accumulated data exceeds 64 bytes, at which point a heap allocation is made. Because `uhash` constructs a fresh `xxhasher` per call, the streaming path is exercised only for large objects that exceed the buffer in a single hash computation.

## When to Use `uhash` vs. `hardened_hash`

The comment in `UnorderedContainers.h` states the rule directly: use `hash_*` containers (and by extension `uhash`) for keys that don't require cryptographic security; use `hardened_hash_*` for keys exposed to externally-controlled input. `uhash` with `xxhasher` is deterministic across program runs, making it unsuitable as the hash function for containers keyed on data from network peers — an attacker who can predict bucket collisions can trigger O(n) lookup degradation. `hardened_hash` mitigates this by seeding the hasher randomly at startup.