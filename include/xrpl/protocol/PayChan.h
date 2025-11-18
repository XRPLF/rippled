#ifndef XRPL_PROTOCOL_PAYCHAN_H_INCLUDED
#define XRPL_PROTOCOL_PAYCHAN_H_INCLUDED

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/XRPAmount.h>

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

}  // namespace ripple

#endif
