#pragma once

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/Serializer.h>

namespace xrpl {

inline void
addSerializeSponsorData(Serializer& msg, AccountID const& sponsorID, std::uint32_t const& flags)
{
    msg.addBitString(sponsorID);
    msg.add32(flags);
}

}  // namespace xrpl

#endif
