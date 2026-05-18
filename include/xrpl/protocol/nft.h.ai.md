# `include/xrpl/protocol/nft.h`

## Role in the System

This header is the single source of truth for the binary layout of an XRPL NFT token identifier. Every 256-bit `NFTokenID` on the ledger is a precisely packed big-endian structure, and this file provides the constants, type aliases, and accessor functions that let the rest of the codebase extract fields from that structure without duplicating bit-offset arithmetic. It also houses `cipheredTaxon()`, the cryptographically lightweight but architecturally critical function that prevents NFT page hotspots.

## NFToken ID Layout

A `uint256` NFT identifier packs six fields sequentially, all in big-endian byte order:

| Byte offset | Size | Field |
|---|---|---|
| 0–1 | 2 bytes | flags (`std::uint16_t`) |
| 2–3 | 2 bytes | transfer fee in basis points (`std::uint16_t`) |
| 4–23 | 20 bytes | issuer `AccountID` |
| 24–27 | 4 bytes | *ciphered* taxon (`std::uint32_t`) |
| 28–31 | 4 bytes | serial / mint sequence (`std::uint32_t`) |

The companion file `nftPageMask.h` defines a `pageMask` constant with zeros in bytes 0–19 and `0xFF` in bytes 20–31. This makes the low 96 bits — the last four bytes of the issuer address plus the ciphered taxon plus the serial — the sort key that determines which `NFTokenPage` ledger object holds a given token. Flags and transfer fee are deliberately excluded from that key; they affect behaviour, not placement.

## Type Safety: `Taxon`

```cpp
struct TaxonTag {};
using Taxon = tagged_integer<std::uint32_t, TaxonTag>;
```

`Taxon` is a `tagged_integer` wrapping `uint32_t`. The empty `TaxonTag` struct serves purely as a phantom type: the compiler will reject any accidental integer passed where a `Taxon` is expected, and vice versa. The two conversion helpers `toTaxon()` and `toUInt32()` are the only intended crossing points. This matters because taxons and serial numbers are the same underlying type and appear side by side in several call sites — a silent mix-up would produce valid-looking but wrong token IDs.

## Flag Constants

Five `constexpr` bit flags are defined:

- `flagBurnable` (0x0001) — the issuer may burn the token even if held by someone else  
- `flagOnlyXRP` (0x0002) — the token may only be traded for XRP  
- `flagCreateTrustLines` (0x0004) — accepting a transfer may open trust lines on the holder's account  
- `flagTransferable` (0x0008) — the token may be transferred to accounts other than the issuer  
- `flagMutable` (0x0010) — the token's URI may be changed post-mint

Because flags live in bytes 0–1 of the token ID, they are immutable after minting: changing a flag would change the token ID itself, breaking all references. `NFTokenModify.cpp` relies directly on `nft::getFlags()` to check `flagMutable` before permitting a URI update — the flag is the authorization.

## The Taxon Cipher (`cipheredTaxon`)

This is the most algorithmically significant function in the file. The ledger stores NFTs in `NFTokenPage` objects sorted by their low 96 bits. If an issuer mints thousands of tokens under the same taxon, all of those tokens would share the same taxon bytes (24–27) and differ only in serial (28–31). Without intervention they would cluster in the same few pages — a hotspot that increases ledger-write fan-out for every mint.

The solution is a Linear Congruential Generator (LCG) seeded by the mint sequence number:

```cpp
return taxon ^ toTaxon(((384160001 * tokenSeq) + 2459));
```

The multiplier 384160001 is congruent to 1 mod 4 and the addend 2459 is odd, satisfying the Hull-Dobell theorem conditions for a full-period LCG over 2³² when arithmetic wraps naturally on `uint32_t`. The result is XORed with the user-supplied taxon to produce the stored value. Because `tokenSeq` advances monotonically per account (and the issuer cannot freely choose it), successive mints under the same taxon land in very different positions in the page sort order, distributing load across multiple pages.

The decoding in `getTaxon()` exploits the fact that XOR is its own inverse: it calls `cipheredTaxon(serial, storedTaxon)` a second time, which cancels the original scramble and recovers the original taxon. No separate "decipher" function is needed.

The comment carries an explicit warning: changing these LCG constants is a consensus-breaking change that would require an amendment and a protocol-level way to distinguish old from new token IDs, because the stored ciphered value is permanent on the ledger.

## Field Accessor Functions

All five accessors use `memcpy` rather than pointer casts for safe unaligned reads, then call `boost::endian::big_to_native` to produce host-endian values:

- `getFlags(id)` — reads bytes 0–1  
- `getTransferFee(id)` — reads bytes 2–3; the value is in basis points (e.g., 50000 = 50%)  
- `getIssuer(id)` — calls `AccountID::fromVoid(id.data() + 4)`, reading bytes 4–23  
- `getTaxon(id)` — reads bytes 24–27, then un-ciphers using the serial recovered from bytes 28–31  
- `getSerial(id)` — reads bytes 28–31

These are used throughout the NFT subsystem: `NFTokenMint::createNFTokenID()` packs the structure; `NFTokenHelpers.cpp` calls `getIssuer()` to resolve royalty recipients and `getFlags()` to enforce transfer restrictions; invariant checkers call `getFlags()` and `getTaxon()` to validate page ordering. The layout is a stable ABI shared across ledger storage, RPC output, and transaction validation.