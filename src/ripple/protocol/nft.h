//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_PROTOCOL_NFT_H_INCLUDED
#define RIPPLE_PROTOCOL_NFT_H_INCLUDED

#include <ripple/basics/base_uint.h>
#include <ripple/basics/tagged_integer.h>
#include <ripple/protocol/AccountID.h>

#include <boost/endian/conversion.hpp>

#include <cstdint>
#include <cstring>

namespace ripple {
namespace nft {

// Separate taxons from regular integers.
struct TaxonTag
{
};
using Taxon = tagged_integer<std::uint32_t, TaxonTag>;

inline Taxon
toTaxon(std::uint32_t i)
{
    return static_cast<Taxon>(i);
}

inline std::uint32_t
toUInt32(Taxon t)
{
    return static_cast<std::uint32_t>(t);
}

constexpr std::uint16_t const flagBurnable = 0x0001;
constexpr std::uint16_t const flagOnlyXRP = 0x0002;
constexpr std::uint16_t const flagCreateTrustLines = 0x0004;
constexpr std::uint16_t const flagTransferable = 0x0008;

inline std::uint16_t
getFlags(uint256 const& id)
{
    std::uint16_t flags;
    memcpy(&flags, id.begin(), 2);
    return boost::endian::big_to_native(flags);
}

inline std::uint16_t
getTransferFee(uint256 const& id)
{
    std::uint16_t fee;
    memcpy(&fee, id.begin() + 2, 2);
    return boost::endian::big_to_native(fee);
}

inline std::uint32_t
getSerial(uint256 const& id)
{
    std::uint32_t seq;
    memcpy(&seq, id.begin() + 28, 4);
    return boost::endian::big_to_native(seq);
}

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

inline Taxon
getTaxon(uint256 const& id)
{
    std::uint32_t taxon;
    memcpy(&taxon, id.begin() + 24, 4);
    taxon = boost::endian::big_to_native(taxon);

    // The taxon cipher is just an XOR, so it is reversible by applying the
    // XOR a second time.
    return cipheredTaxon(getSerial(id), toTaxon(taxon));
}

inline AccountID
getIssuer(uint256 const& id)
{
    return AccountID::fromVoid(id.data() + 4);
}

}  // namespace nft
}  // namespace ripple

#endif
