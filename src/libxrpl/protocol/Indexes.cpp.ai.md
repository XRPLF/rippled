# `src/libxrpl/protocol/Indexes.cpp`

## Role and Purpose

Every object stored in the XRPL ledger state map is addressed by a 256-bit key. This file is the single authoritative source for computing those keys. It implements the complete set of keylet factory functions and the lower-level index utilities that the rest of the codebase uses to locate any ledger entry — from account roots and trust lines to NFT pages, cross-chain bridges, and automated market makers.

## Tagged Hashing: The `LedgerNameSpace` Enum

The foundation of the entire file is `LedgerNameSpace`, a `uint16_t`-backed enum that assigns a unique single-character discriminator to each ledger object type. Every index is derived from a `sha512Half` over the namespace tag prepended to the object's parameters:

```cpp
template <class... Args>
static uint256
indexHash(LedgerNameSpace space, Args const&... args)
{
    return sha512Half(safe_cast<std::uint16_t>(space), args...);
}
```

This is "tagged hashing" — the same technique used elsewhere in cryptographic protocols to prevent cross-context collisions. Without the discriminator, two different object types sharing the same parameters (e.g., an offer and an escrow both keyed by account + sequence) could accidentally land on the same ledger key. The namespace prefix makes every hash domain-separated by construction.

The `LedgerNameSpace` values are permanent protocol constants. The file comment is explicit: changing an existing value would make every on-ledger object of that type unaddressable without a coordinated migration. Three legacy entries (`CONTRACT`, `GENERATOR`, `NICKNAME`) are kept as `[[deprecated]]` not for backward compatibility, but to reserve their character codes so they are never accidentally reused for a new object type.

## The `Keylet` Abstraction

The public API returns `Keylet` values — a simple struct pairing a `uint256` key with a `LedgerEntryType`. This type-tagged key serves as a typed handle: any code that fetches a ledger entry using a `Keylet` knows statically what kind of object it expects, and `Keylet::check()` can validate the actual type at runtime. The pattern prevents bugs where, say, a function looking up a signer list accidentally accepts an account root that happens to sit at the same address in some edge case. Most factory functions in the `keylet::` namespace return owned `Keylet` values; singleton objects return `Keylet const&` to a static local computed exactly once.

## Symmetric Object Identification

Several ledger objects are intrinsically symmetric between two parties, and the key derivation must be order-independent. Two design patterns handle this.

**Trust lines** (`keylet::line`) use `std::minmax` to sort the two account IDs before hashing. A trust line between Alice and Bob is the same on-ledger object regardless of which side you query from:

```cpp
auto const accounts = std::minmax(id0, id1);
return {ltRIPPLE_STATE,
        indexHash(LedgerNameSpace::TRUST_LINE,
                  accounts.first, accounts.second, currency)};
```

**AMM pools** (`keylet::amm`) apply the same technique with `std::minmax` on the two `Asset` values, then dispatch on all four combinations of `Issue`/`MPTIssue` pairs through `if constexpr` branches inside a `std::visit`. This compile-time dispatch avoids virtual dispatch while handling the heterogeneous token type combinations introduced with multi-purpose tokens (MPTs).

## Order Book Quality Encoding

Order books in the XRPL ledger are implemented as sorted directories. Rather than store price data separately, the quality (exchange rate) of a directory page is embedded directly in the last 8 bytes of the key:

```cpp
Keylet
quality(Keylet const& k, std::uint64_t q) noexcept
{
    uint256 x = k.key;
    ((std::uint64_t*)x.end())[-1] = boost::endian::native_to_big(q);
    return {ltDIR_NODE, x};
}
```

The big-endian encoding is deliberate: since `uint256` values are stored and compared in memory order (most significant byte first), incrementing the embedded quality field via `getQualityNext` advances to the next order book page in natural sort order. `getQuality()` recovers the 64-bit value from the same position using `boost::endian::big_to_native`. The code itself acknowledges the ugliness of the raw pointer arithmetic (`// FIXME This is ugly`), but correctness relies on `base_uint`'s internal big-endian byte layout.

## NFT Page Addressing: Composite Keys Without Hashing

NFT token page identifiers break the pattern of every other object type. Instead of hashing inputs, the 256-bit key is constructed directly as a composite value:

- The high 160 bits hold the owner's `AccountID`.
- The low 96 bits hold a range tag derived from a specific NFToken ID masked by `nft::pageMask`.

```cpp
Keylet nftpage_min(AccountID const& owner)
{
    std::array<std::uint8_t, 32> buf{};
    std::memcpy(buf.data(), owner.data(), owner.size());
    return {ltNFTOKEN_PAGE, uint256{buf}};
}

Keylet nftpage(Keylet const& k, uint256 const& token)
{
    return {ltNFTOKEN_PAGE, (k.key & ~nft::pageMask) + (token & nft::pageMask)};
}
```

This design gives all of a given owner's NFT pages a contiguous range in the SHAMap, bounded by `nftpage_min` (low 96 bits all zero) and `nftpage_max` (low 96 bits all one). Traversing an owner's NFT collection is therefore a bounded range scan rather than a linked-list walk. No two owners' page ranges can overlap because the high 160 bits uniquely identify the owner.

## Singleton Keylets

The `amendments()`, `fees()`, `negativeUNL()`, and no-arg `skip()` functions return `Keylet const&` to a function-local static. These objects are globally unique in any ledger — they take no differentiating parameters — so computing their hash once is correct and efficient. The pattern also serves as self-documenting intent: the `const&` return type signals to callers that they are retrieving a well-known fixed address, not constructing a new one.

## Multi-Purpose Token (MPT) Identifiers

`makeMptID` constructs the 192-bit `MPTID` for a token issuance by packing a big-endian sequence number into the first 4 bytes and an `AccountID` into the next 20 bytes. The explicit `native_to_big` conversion ensures the composite identifier has a canonical byte order regardless of the host platform. The `mptIssuance` and `mptoken` keylets build on this, with `mptoken` accepting either a raw `MPTID` or the pre-computed 256-bit issuance key to avoid redundant hashing when the issuance key is already available.

## DepositPreauth with Credentials

The credential-set overload of `depositPreauth` handles a variable number of (issuer, credentialType) pairs:

```cpp
std::vector<uint256> hashes;
for (auto const& o : authCreds)
    hashes.emplace_back(sha512Half(o.first, o.second));
return {ltDEPOSIT_PREAUTH,
        indexHash(LedgerNameSpace::DEPOSIT_PREAUTH_CREDENTIALS, owner, hashes)};
```

Each credential is hashed individually first. The resulting hashes are then passed as a vector to the outer `indexHash`. Because the caller is required to pass a `std::set` (which provides deterministic sorted order), the final hash is stable regardless of insertion order. A distinct namespace (`DEPOSIT_PREAUTH_CREDENTIALS` vs `DEPOSIT_PREAUTH`) ensures single-account and credential-set preauth objects occupy separate key spaces and cannot collide even with an identical owner.

## Signer List Pagination

The paginated form `signers(account, page)` is kept `static` — private to the translation unit — because only page 0 is ever allocated. The comment explicitly notes this as an architectural reservation: if signer list pagination is someday needed, the infrastructure to derive per-page keylets is already present, but the interface is intentionally not exposed until the feature exists.