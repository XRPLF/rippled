# `nftPageMask.h` — NFToken Page Ordering Mask

This tiny header defines a single `constexpr uint256` constant, `xrpl::nft::pageMask`, that acts as the authoritative boundary between two conceptual regions of an `NFTokenID`.

## The NFToken ID Layout Problem

Every `NFTokenID` in XRPL is a 256-bit value. By protocol convention the high 160 bits encode the issuing `AccountID` and the low 96 bits encode token-specific metadata (taxon and sequence number). NFToken ownership pages in the ledger — `ltNFTOKEN_PAGE` SLEs — are keyed by a `uint256` that fuses an owner's `AccountID` into the high bits while placing a token-derived value in the low 96 bits. To split those two regions consistently across every site that constructs or inspects page keys, the codebase needs a single, canonical bitmask. That is exactly what this file provides.

## The Constant

```cpp
uint256 constexpr pageMask(
    "0000000000000000000000000000000000000000ffffffffffffffffffffffff");
```

The mask is 96 bits of `1` in the least-significant position and 160 bits of `0` above. It is constructed via the `constexpr` `std::string_view` overload of `base_uint`, so the value is resolved at compile time with zero runtime overhead.

## How It Is Used

`pageMask` appears at every point where code must transition between the owner-identity portion and the token-ordering portion of a page key.

**Page key construction** in `Indexes.cpp` (`keylet::nftpage`) computes:

```cpp
(k.key & ~nft::pageMask) + (token & nft::pageMask)
```

The complement `~pageMask` isolates the owner's high 160 bits from an existing page keylet, while `token & pageMask` extracts the low 96 bits of the incoming token ID. Adding them (which is safe because the two regions are disjoint) forms the new page key.

**The max-page sentinel** (`keylet::nftpage_max`) sets the entire `uint256` to `pageMask` (all-ones in the low 96 bits) and then overwrites the high bytes with the owner's `AccountID`, yielding the lexicographically largest page key for that owner. This sentinel is used as the tail anchor of the doubly-linked page chain.

**Pagination in RPC handlers** (`AccountNFTs.cpp`, `AccountObjects.cpp`) applies the mask to validate that a client-supplied `marker` or `entryIndex` refers to the correct page:

```cpp
uint256 const maskedMarker = marker & nft::pageMask;
// ...
if (firstNFTPage.key != (entryIndex & ~nft::pageMask))
```

Using `~pageMask` isolates the owner part of the key; using `pageMask` itself isolates the token-ordering part.

**Sort-order comparisons** in `NFTokenHelpers.cpp` use the mask to establish ordering among tokens on the same page:

```cpp
if (auto const lowBitsCmp{(a & nft::pageMask) <=> (b & nft::pageMask)}; lowBitsCmp != 0)
```

Tokens with different low-96-bit values belong to different pages; tokens sharing those bits belong to the same page and are distinguished by their full 256-bit ID.

**Invariant checking** in `NFTInvariant.cpp` verifies that a page being deleted has its low 96 bits all-ones (the sentinel value) and that non-sentinel pages satisfy structural constraints, both using direct comparison against `pageMask`.

## Design Rationale

Centralising this literal in one header eliminates the risk of transcription errors that would silently corrupt page key lookups. Marking it `constexpr` rather than `inline` or `static` means every translation unit that includes the header gets it as a true compile-time constant with no ODR concerns — `uint256` satisfies `constexpr` construction via `base_uint`'s `string_view` constructor, making this a zero-cost abstraction at the protocol boundary.