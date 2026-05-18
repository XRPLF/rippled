# `HashPrefix.h` — Protocol Hash Domain Separation

## Purpose

`HashPrefix.h` defines a set of 4-byte sentinel values that are prepended to binary data before it is hashed anywhere in the XRPL protocol. The fundamental problem it solves is **hash domain collision**: two structurally different objects that happen to share the same serialized bytes would otherwise hash to the same digest. By prefixing every hash input with a type-specific constant, the XRPL protocol guarantees that a transaction hash and an account-state hash with identical raw bytes will always be different values.

These constants are **immutable protocol artifacts**. The file's own comment states this plainly: changing the type or value of any prefix breaks consensus and cross-node compatibility.

## Construction: `make_hash_prefix`

The private helper `detail::make_hash_prefix(char a, char b, char c)` packs three ASCII characters into the high 24 bits of a `uint32_t`, leaving the low 8 bits as zero:

```
result = (a << 24) | (b << 16) | (c << 8)
```

This is a deliberate engineering choice. The trailing zero byte acts as an implicit separator and ensures no prefix value can accidentally equal a valid one-byte or two-byte byte sequence. The ASCII mnemonic letters make the prefixes self-documenting in hex dumps — `HashPrefix::transactionID` becomes `0x54584E00` (`T`, `X`, `N`, `\0`), instantly recognizable when inspecting raw network data or database files.

## The Enum

`HashPrefix` is a strongly-typed `enum class : std::uint32_t` with twelve members, each covering a distinct context in which hashing occurs:

| Enumerator | Mnemonic | Usage context |
|---|---|---|
| `transactionID` | TXN | Hashing a bare transaction to produce its canonical ID |
| `txNode` | SND | Hashing a transaction plus its execution metadata |
| `leafNode` | MLN | Hashing an account-state leaf node in the SHAMap |
| `innerNode` | MIN | Hashing a SHAMap inner (branch) node |
| `ledgerMaster` | LWR | Hashing ledger header data for signing |
| `txSign` | STX | Hashing a transaction body for single signing |
| `txMultiSign` | SMT | Hashing a transaction body for multi-signing |
| `validation` | VAL | Hashing a validator validation message |
| `proposal` | PRP | Hashing a consensus proposal |
| `manifest` | MAN | Hashing a validator manifest for signing |
| `paymentChannelClaim` | CLM | Hashing a payment channel off-ledger claim |
| `batch` | BCH | Hashing batch transaction data |

The separation between `txSign` and `txMultiSign` is particularly important: a single-signature blob cannot be replayed as a multi-signature contribution, because the hash inputs differ even when the transaction serialization is byte-for-byte identical.

## Integration with `hash_append`

The file provides a template free function:

```cpp
template <class Hasher>
void hash_append(Hasher& h, HashPrefix const& hp) noexcept;
```

This conforms to the N3980 "Types Don't Know #" protocol used throughout the beast/xrpl hashing infrastructure. By implementing `hash_append`, a `HashPrefix` value can be composed seamlessly with other objects in a variadic call to `sha512Half` from `digest.h`:

```cpp
sha512Half(HashPrefix::transactionID, data)
```

That call hashes the 32-bit prefix value followed immediately by the transaction bytes, all in a single SHA-512 pass. No temporary allocations, no explicit serialization step — the hash_append machinery feeds each argument directly into the running digest state.

## Two Call Styles in Practice

Callers use `HashPrefix` via two distinct patterns, both leading to the same prefix-then-data layout:

1. **Serializer prefix** — used in `Sign.cpp`, `PayChan.h`, and ledger code: `ss.add32(HashPrefix::txSign)` writes the raw uint32 into a `Serializer` buffer before appending the object's signing fields. This is the style used when a signature or ledger hash must be computed over a Serializer-built blob.

2. **`hash_append` composition** — used in `SHAMapInnerNode.cpp` and `sha512Half` calls in the SHAMap layer: `hash_append(h, HashPrefix::innerNode)` feeds the prefix directly into a streaming hasher alongside node data, avoiding an intermediate buffer.

Both paths produce the same 4-byte prefix at position zero of the hash input, which is the only invariant that matters for domain separation.

## Why This Matters for Security

Without domain separation, a carefully crafted account state ledger object could be constructed with bytes identical to a transaction, letting an attacker claim a false transaction ID or forge a signing payload. The prefix scheme closes this class of attack at the protocol layer, not at the application layer, so every code path that hashes XRPL data automatically operates in an isolated namespace regardless of where or how it computes that hash.