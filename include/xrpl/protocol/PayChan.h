#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>

namespace xrpl {

inline void
serializePayChanAuthorization(Serializer& msg, uint256 const& key, XRPAmount const& amt)
{
    msg.add32(HashPrefix::PaymentChannelClaim);
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
    msg.add32(HashPrefix::PaymentChannelClaim);
    msg.addBitString(key);
    if (amt == beast::kZero)
    {
        msg.add64(STAmount::kIssuedCurrency);
    }
    else if (amt.signum() == -1)
    {  // 512 = not native; the sign is encoded by omitting the 256 flag, so
       // the mantissa must be serialized as its absolute value
        msg.add64(
            static_cast<std::uint64_t>(-amt.mantissa()) |
            (static_cast<std::uint64_t>(amt.exponent() + 512 + 97) << (64 - 10)));
    }
    else
    {  // 256 = positive
        msg.add64(
            amt.mantissa() |
            (static_cast<std::uint64_t>(amt.exponent() + 512 + 256 + 97) << (64 - 10)));
    }
    msg.addBitString(cur);
    msg.addBitString(iss);
}

inline void
serializePayChanAuthorization(
    Serializer& msg,
    uint256 const& key,
    MPTAmount const& amt,
    MPTID const& mptID,
    AccountID const& iss)
{
    msg.add32(HashPrefix::PaymentChannelClaim);
    msg.addBitString(key);
    msg.add64(amt.value());
    msg.addBitString(mptID);
    msg.addBitString(iss);
}

inline void
serializePayChanAuthorization(Serializer& msg, uint256 const& key, STAmount const& amt)
{
    if (amt.native())
    {
        serializePayChanAuthorization(msg, key, amt.xrp());
    }
    else if (amt.holds<Issue>())
    {
        serializePayChanAuthorization(
            msg, key, amt.iou(), amt.get<Issue>().currency, amt.get<Issue>().account);
    }
    else if (amt.holds<MPTIssue>())
    {
        auto const& mpt = amt.get<MPTIssue>();
        auto const& mptID = mpt.getMptID();
        serializePayChanAuthorization(msg, key, amt.mpt(), mptID, amt.getIssuer());
    }
}

}  // namespace xrpl
