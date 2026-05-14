/** @file
 *  Binary layout and field accessors for XRPL NFToken identifiers.
 *
 *  Every `NFTokenID` on the ledger is a 256-bit big-endian structure that
 *  packs six fields: flags (2 bytes), transfer fee (2 bytes), issuer
 *  AccountID (20 bytes), ciphered taxon (4 bytes), and serial number
 *  (4 bytes). This file is the single source of truth for that layout;
 *  all other subsystems that need to inspect a token ID call the accessors
 *  defined here rather than duplicating the byte-offset arithmetic.
 *
 *  @see nftPageMask.h for the `kPAGE_MASK` constant that isolates the low
 *      96 bits used as the NFTokenPage sort key.
 */
#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/tagged_integer.h>
#include <xrpl/protocol/AccountID.h>

#include <boost/endian/conversion.hpp>

#include <cstdint>
#include <cstring>

namespace xrpl::nft {

/** Phantom type tag that makes `Taxon` a distinct type from plain integers.
 *
 *  This empty struct exists solely to instantiate `TaggedInteger` so that
 *  the compiler rejects accidental integer/taxon mixups at call sites where
 *  taxon and serial number (both `uint32_t` underneath) appear side by side.
 */
struct TaxonTag
{
};

/** Strongly-typed wrapper for an NFT taxon value.
 *
 *  A taxon is an issuer-assigned 32-bit category label embedded in every
 *  NFTokenID. The compiler distinguishes `Taxon` from raw `uint32_t`, so a
 *  serial number cannot be passed where a taxon is expected. Use `toTaxon()`
 *  and `toUInt32()` to cross the type boundary explicitly.
 */
using Taxon = TaggedInteger<std::uint32_t, TaxonTag>;

/** Convert a plain `uint32_t` to a `Taxon`.
 *
 *  @param i The raw taxon value obtained from a transaction field or other
 *      integer source.
 *  @return A `Taxon` wrapping `i`.
 */
inline Taxon
toTaxon(std::uint32_t i)
{
    return static_cast<Taxon>(i);
}

/** Convert a `Taxon` to a plain `uint32_t`.
 *
 *  @param t The taxon to unwrap.
 *  @return The underlying integer value.
 */
inline std::uint32_t
toUInt32(Taxon t)
{
    return static_cast<std::uint32_t>(t);
}

/** The issuer may burn the token even when it is held by another account. */
constexpr std::uint16_t const kFLAG_BURNABLE = 0x0001;

/** The token may only be bought or sold for XRP, not IOUs. */
constexpr std::uint16_t const kFLAG_ONLY_XRP = 0x0002;

/** Accepting a transfer of this token may open trust lines on the recipient's
 *  account to receive IOU royalty payments.
 *
 *  @note Under amendment `fixEnforceNFTokenTrustline`, transfers with a
 *      non-zero transfer fee denominated in an IOU are rejected with
 *      `tecNO_LINE` unless this flag is set or the NFT issuer is also the
 *      IOU issuer.
 */
constexpr std::uint16_t const kFLAG_CREATE_TRUST_LINES = 0x0004;

/** The token may be transferred to accounts other than the issuer. */
constexpr std::uint16_t const kFLAG_TRANSFERABLE = 0x0008;

/** The token's URI may be updated after minting via `NFTokenModify`.
 *
 *  Because flags are baked into bytes 0–1 of the token ID, this flag — like
 *  all flags — is immutable after minting. `NFTokenModify` reads it directly
 *  from the token ID via `getFlags()` and returns `tecNO_PERMISSION` if it
 *  is absent.
 */
constexpr std::uint16_t const kFLAG_MUTABLE = 0x0010;

/** Extract the flags field from an NFTokenID.
 *
 *  Reads bytes 0–1 (big-endian) of `id` and converts to host byte order.
 *  Flags are immutable after minting because they are part of the token ID.
 *
 *  @param id A 256-bit NFTokenID as stored on the ledger.
 *  @return The 16-bit flags bitmask; compare against the `kFLAG_*` constants.
 */
inline std::uint16_t
getFlags(uint256 const& id)
{
    std::uint16_t flags = 0;
    memcpy(&flags, id.begin(), 2);
    return boost::endian::big_to_native(flags);
}

/** Extract the transfer fee from an NFTokenID.
 *
 *  Reads bytes 2–3 (big-endian) of `id` and converts to host byte order.
 *  The fee is expressed in basis points; 50000 represents 50%. Use
 *  `nft::transferFeeAsRate()` (in `Rate.h`) to convert to a `Rate` suitable
 *  for `multiply()`.
 *
 *  @param id A 256-bit NFTokenID as stored on the ledger.
 *  @return Transfer fee in basis points (0–50000).
 */
inline std::uint16_t
getTransferFee(uint256 const& id)
{
    std::uint16_t fee = 0;
    memcpy(&fee, id.begin() + 2, 2);
    return boost::endian::big_to_native(fee);
}

/** Extract the serial number from an NFTokenID.
 *
 *  Reads bytes 28–31 (big-endian) of `id` and converts to host byte order.
 *  The serial is the issuer's mint sequence at the time the token was created
 *  and is unique within a single issuer's token space.
 *
 *  @param id A 256-bit NFTokenID as stored on the ledger.
 *  @return The 32-bit serial number.
 */
inline std::uint32_t
getSerial(uint256 const& id)
{
    std::uint32_t seq = 0;
    memcpy(&seq, id.begin() + 28, 4);
    return boost::endian::big_to_native(seq);
}

/** Compute the ciphered taxon stored in an NFTokenID for a given mint.
 *
 *  To prevent tokens minted under the same taxon from clustering in the same
 *  `NFTokenPage` objects (a ledger-write hotspot), the stored taxon is XORed
 *  with a scramble value derived from the mint sequence number via a Linear
 *  Congruential Generator:
 *
 *  @code
 *  ciphered = taxon ^ (384160001 * tokenSeq + 2459)
 *  @endcode
 *
 *  The LCG multiplier (384160001 ≡ 1 mod 4) and addend (2459, odd) satisfy
 *  the Hull-Dobell theorem, guaranteeing a full period over 2³² when
 *  arithmetic wraps naturally on `uint32_t`. Because `tokenSeq` advances
 *  monotonically and the issuer cannot choose it freely, successive mints
 *  under the same taxon land in very different positions in the page sort
 *  order, distributing load.
 *
 *  Since XOR is its own inverse, calling this function a second time with the
 *  stored ciphered value recovers the original taxon — no separate decipher
 *  function is needed. `getTaxon()` relies on this property.
 *
 *  @param tokenSeq The issuer's account sequence at the time of minting
 *      (the serial number that will be embedded in the token ID).
 *  @param taxon The issuer-supplied taxon to cipher.
 *  @return The ciphered taxon value to embed in bytes 24–27 of the token ID.
 *
 *  @note **Protocol-stable constants.** The LCG parameters 384160001 and
 *      2459 are embedded in every NFTokenID ever minted. Changing them is
 *      a consensus-breaking change that requires an amendment and a way to
 *      distinguish token IDs generated under the old scheme.
 */
inline Taxon
cipheredTaxon(std::uint32_t tokenSeq, Taxon taxon)
{
    // An issuer may issue several NFTs with the same taxon; to ensure that NFTs
    // are spread across multiple pages we lightly mix the taxon up by using the
    // sequence (which is not under the issuer's direct control) as the seed for
    // a simple linear congruential generator.
    //
    // From the Hull-Dobell theorem we know that f(x)=(m*x+c) mod n will yield a
    // permutation of [0, n) when n is a power of 2 if m is congruent to 1 mod 4
    // and c is odd.
    //
    // Here we use m = 384160001 and c = 2459. The modulo is implicit because we
    // use 2^32 for n and the arithmetic gives it to us for "free".
    //
    // Note that the scramble value we calculate is not cryptographically secure
    // but that's fine since all we're looking for is some dispersion.
    //
    // **IMPORTANT** Changing these numbers would be a breaking change requiring
    //               an amendment along with a way to distinguish token IDs that
    //               were generated with the old code.
    return taxon ^ toTaxon(((384160001 * tokenSeq) + 2459));
}

/** Extract and decode the taxon from an NFTokenID.
 *
 *  Reads bytes 24–27 (big-endian) to obtain the stored ciphered taxon, then
 *  deciphers it by calling `cipheredTaxon()` a second time with the serial
 *  number from bytes 28–31. Because XOR is its own inverse, this cancels the
 *  original scramble and returns the issuer-supplied taxon value.
 *
 *  @param id A 256-bit NFTokenID as stored on the ledger.
 *  @return The original issuer-assigned taxon, not the ciphered value.
 */
inline Taxon
getTaxon(uint256 const& id)
{
    std::uint32_t taxon = 0;
    memcpy(&taxon, id.begin() + 24, 4);
    taxon = boost::endian::big_to_native(taxon);

    // The taxon cipher is just an XOR, so it is reversible by applying the
    // XOR a second time.
    return cipheredTaxon(getSerial(id), toTaxon(taxon));
}

/** Extract the issuer AccountID from an NFTokenID.
 *
 *  Reads bytes 4–23 of `id`, which hold the 20-byte issuer address in
 *  big-endian order as packed by `NFTokenMint::createNFTokenID()`.
 *
 *  @param id A 256-bit NFTokenID as stored on the ledger.
 *  @return The 20-byte AccountID of the token's issuer.
 */
inline AccountID
getIssuer(uint256 const& id)
{
    return AccountID::fromVoid(id.data() + 4);
}

}  // namespace xrpl::nft
