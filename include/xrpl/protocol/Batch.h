#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Serializer.h>

#include <cstdint>
#include <vector>

namespace xrpl {

/**
 * Serialize the data that a batch signer signs.
 *
 * @param prefix HashPrefix::Batch when the batch signer signs on its own,
 * HashPrefix::BatchMultiSign when the signature comes from a signer list. The
 * two forms differ by the signer account that the caller appends, so the
 * prefix keeps them in separate hash spaces, as with TxSign and TxMultiSign.
 */
inline void
serializeBatch(
    Serializer& msg,
    HashPrefix prefix,
    AccountID const& outerAccount,
    std::uint32_t outerSeqValue,
    std::uint32_t const& flags,
    std::vector<uint256> const& txids)
{
    msg.add32(prefix);
    msg.addBitString(outerAccount);
    msg.add32(outerSeqValue);
    msg.add32(flags);
    msg.add32(std::uint32_t(txids.size()));
    for (auto const& txid : txids)
        msg.addBitString(txid);
}

}  // namespace xrpl
