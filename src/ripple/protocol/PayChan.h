//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_PAYCHAN_H_INCLUDED
#define RIPPLE_PROTOCOL_PAYCHAN_H_INCLUDED

#include <ripple/basics/XRPAmount.h>
#include <ripple/basics/base_uint.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/Serializer.h>

namespace ripple {

inline void
serializePayChanAuthorization(
    Serializer& msg,
    uint256 const& key,
    XRPAmount const& amt)
{
    msg.add32(HashPrefix::paymentChannelClaim);
    msg.addBitString(key);
    msg.add64(amt.drops());
}

inline void
serializePayChanAuthorization(
    Serializer& msg,
    uint256 const& key,
    IOUAmount const& amt,
    Currency const& cur,
    AccountID const& iss)
{
    msg.add32(HashPrefix::paymentChannelClaim);
    msg.addBitString(key);
    if (amt == beast::zero)
        msg.add64(STAmount::cNotNative);
    else if (amt.signum() == -1)  // 512 = not native
        msg.add64(
            amt.mantissa() |
            (static_cast<std::uint64_t>(amt.exponent() + 512 + 97)
             << (64 - 10)));
    else  // 256 = positive
        msg.add64(
            amt.mantissa() |
            (static_cast<std::uint64_t>(amt.exponent() + 512 + 256 + 97)
             << (64 - 10)));
    msg.addBitString(cur);
    msg.addBitString(iss);
}

}  // namespace ripple

#endif
