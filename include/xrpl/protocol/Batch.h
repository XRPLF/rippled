#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Serializer.h>

#include <cstdint>
#include <vector>

namespace xrpl {

inline void
serializeBatch(
    Serializer& msg,
    AccountID const& outerAccount,
    std::uint32_t outerSeqValue,
    std::uint32_t const& flags,
    std::vector<uint256> const& txids)
{
    msg.add32(HashPrefix::Batch);
    msg.addBitString(outerAccount);
    msg.add32(outerSeqValue);
    msg.add32(flags);
    msg.add32(std::uint32_t(txids.size()));
    for (auto const& txid : txids)
        msg.addBitString(txid);
}

}  // namespace xrpl
