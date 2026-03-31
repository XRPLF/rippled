#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/XRPAmount.h>

namespace xrpl {

inline void
serializePayChanAuthorization(Serializer& msg, uint256 const& key, XRPAmount const& amt)
{
    msg.add32(HashPrefix::paymentChannelClaim);
    msg.addBitString(key);
    msg.add64(amt.drops());
}

}  // namespace xrpl
