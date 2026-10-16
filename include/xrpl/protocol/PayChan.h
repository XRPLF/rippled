#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/XRPAmount.h>

namespace xrpl {

/**
 * Serialize the PaymentChannelClaim authorization message:
 * HashPrefix::PaymentChannelClaim, the channel key and the drops as a
 * 64-bit integer.
 */
inline void
serializePayChanAuthorization(Serializer& msg, uint256 const& key, XRPAmount const& amt)
{
    msg.add32(HashPrefix::PaymentChannelClaim);
    msg.addBitString(key);
    msg.add64(amt.drops());
}

/**
 * Serialize the PaymentChannelClaim authorization message for any asset: a
 * native amount uses the XRP layout; a token amount follows
 * HashPrefix::PaymentChannelClaim and the channel key with STAmount::add, the
 * Amount field value without its field header.
 */
inline void
serializePayChanAuthorization(Serializer& msg, uint256 const& key, STAmount const& amt)
{
    if (amt.native())
    {
        serializePayChanAuthorization(msg, key, amt.xrp());
        return;
    }
    msg.add32(HashPrefix::PaymentChannelClaim);
    msg.addBitString(key);
    amt.add(msg);
}

}  // namespace xrpl
