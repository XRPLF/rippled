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

#ifndef RIPPLE_PROTOCOL_CFT_H_INCLUDED
#define RIPPLE_PROTOCOL_CFT_H_INCLUDED

#include <ripple/basics/base_uint.h>
#include <string_view>

namespace ripple {
namespace cft {

inline std::uint32_t
getSequence(uint192 const& issuanceID)
{
    std::uint32_t seq;
    memcpy(&seq, issuanceID.begin(), 4);
    return boost::endian::big_to_native(seq);
}

inline AccountID
getIssuer(uint192 const& issuanceID)
{
    return AccountID::fromVoid(issuanceID.data() + 4);
}


inline uint192
createCFTokenIssuanceID(
    std::uint32_t sequence,
    AccountID const& issuer)
{
    sequence = boost::endian::native_to_big(sequence);

    std::array<std::uint8_t, 24> buf{};

    auto ptr = buf.data();

    std::memcpy(ptr, &sequence, sizeof(sequence));
    ptr += sizeof(sequence);

    std::memcpy(ptr, issuer.data(), issuer.size());
    ptr += issuer.size();
    assert(std::distance(buf.data(), ptr) == buf.size());

    return uint192::fromVoid(buf.data());
}

}  // namespace cft
}  // namespace ripple

#endif
