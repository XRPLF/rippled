# `base_uint.h` — Fixed-Width Big-Endian Integers for the XRP Ledger Protocol

## Role in the System

`base_uint<Bits, Tag>` is the foundational integer type for every fixed-width hash or identifier used in the XRP Ledger. Transaction hashes, ledger hashes, account IDs, currency codes, directory indices, node IDs, and MPT issuance IDs all live as instantiations of this template. The four concrete aliases `uint128`, `uint160`, `uint192`, and `uint256` are defined here; protocol-layer aliases like `AccountID`, `Currency`, `NodeID`, and `Directory` are defined in `UintTypes.h` and `AccountID.h` using the tagged form.

The file originated in the Bitcoin codebase (copyright notice from 2009–2011), was adapted for Ripple/XRP, and has accumulated XRPL-specific concerns such as hardened hashing, `Slice` interoperability, and tag-based type safety.

## Big-Endian Storage Is a Protocol Invariant

The most important design constraint is stated in the class comment: **the internal representation is big-endian and is part of the binary wire protocol**. This isn't just a performance choice; changing it would corrupt serialized ledger data.

Internally, the value is stored as `std::array<std::uint32_t, Bits/32>`, and the comment acknowledges that the array is "really big-endian in byte order" while using 32-bit words for speed. Every arithmetic operation that traverses the array must respect this: `operator++`, `operator--`, and `operator+=` all call `boost::endian::big_to_native` before doing native arithmetic and `boost::endian::native_to_big` when writing back. This per-operation conversion is the price paid for using `uint32_t` words rather than raw `uint8_t` bytes.

## Tag Parameter for Protocol-Level Type Safety

The second template parameter `Tag` exists solely to make `base_uint<160, AccountIDTag>` and `base_uint<160, CurrencyTag>` incompatible types even though they have identical representations. Without `Tag`, an `AccountID` and a `Currency` would be the same C++ type, and the compiler couldn't catch accidental mixing. Tags are empty structs with nothing in them — their sole purpose is to name otherwise-identical instantiations as distinct. This is the phantom-type pattern, and it's used extensively across the protocol layer.

## Hex Parsing: A Baked-In Byteswap

The private `parseFromStringView()` method contains a non-obvious bit-manipulation trick. For each 32-bit word it consumes eight hex characters and places them using the shift sequence `{4, 0, 12, 8, 20, 16, 28, 24}`. This interleaving directly constructs the `uint32_t` value so that when its bytes are read from memory, they appear in the same big-endian order as the hex string — without a separate `native_to_big` call at the end. The first hex character (most significant nibble of the big-endian representation) is placed at bits 4–7, which on a little-endian platform lands in the lowest-address byte. The effect is that the in-memory byte sequence equals the hex string's byte sequence, satisfying the big-endian invariant efficiently.

The parser accepts the special input `"0"` as a shorthand for the all-zero value, regardless of the expected width. Any other string must be exactly `2 * bytes` characters long; otherwise `ParseResult::badLength` is returned. This two-outcome design (via `Expected<decltype(data_), ParseResult>`) feeds both the `[[nodiscard]] bool parseHex()` path (which returns false on error) and the throwing `explicit constexpr base_uint(std::string_view)` constructor. The comment notes this constructor is intended for compile-time use and suggests it become `consteval` in C++23.

## Comparison: Why Byte-by-Byte Works

The spaceship operator `<=>` compares the two values using `std::mismatch` across the raw byte iterators. A comment explicitly explains why this is correct: because the data is stored in big-endian byte order, a byte-by-byte lexicographic comparison of the raw bytes produces the same result as a numeric comparison of the integers. A FIXME note records that `std::lexicographical_compare_three_way` would be preferable but was unavailable on macOS at the time of writing.

## Hashing: Seeded and Raw

`base_uint` exposes two hashing interfaces. The `using hasher = hardened_hash<>` member makes the hash seeded per-container construction using a random 128-bit seed (via `hardened_hash.h`). This resists hash-flooding attacks on `unordered_map`s keyed by protocol values. The `hash_append` friend function feeds raw memory directly to the hash algorithm without any endian conversion, which is correct because the bytes are already in a canonical form. The `beast::is_uniquely_represented` specialization at the bottom of the file asserts that there are no padding bytes, permitting hash algorithms to hash the whole object as a contiguous byte sequence.

The `extract()` specialization for `uint256` is wired into `partitioned_unordered_map`, which uses the extracted value to choose a shard. It reads the first `sizeof(std::size_t)` bytes via `memcpy` to avoid undefined behavior from potentially unaligned access, and the comment notes this will compile to an equivalent direct load on most platforms.

## Container Interface and `fromVoid`

`base_uint` exposes a byte-level STL container interface (`begin()`, `end()`, `data()`, `size()`) with `value_type = unsigned char`. This lets it be treated as a byte range by serialization code, stream operators, and `Slice`-based APIs. The templated constructor and assignment operator accept any `is_contiguous_container` (including `Slice`) and `memcpy` the bytes in, with an assertion that sizes match exactly. The `fromVoid(void const*)` factory and its checked variant `fromVoidChecked` provide a controlled path from raw pointers, with the internal `VoidHelper` tag struct ensuring the ambiguity-prone `base_uint(0)` call routes to the `uint64_t` constructor rather than the raw-pointer one.

## Deprecated Convenience Members

`isZero()`, `isNonZero()`, and `zero()` are marked deprecated; the preferred idiom is comparison against `beast::zero` and assignment from `beast::zero`. The zero-value constructor `base_uint(beast::Zero)` and the assignment `operator=(beast::Zero)` both zero-fill the internal array, integrating with the `beast::Zero` sentinel type used across the codebase.