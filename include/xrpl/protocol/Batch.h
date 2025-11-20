#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/Serializer.h>

namespace ripple {

inline void
serializeBatch(
    Serializer& msg,
    std::uint32_t const& flags,
    std::vector<uint256> const& txids)
{
    msg.add32(HashPrefix::batch);
    msg.add32(flags);
    msg.add32(std::uint32_t(txids.size()));
    for (auto const& txid : txids)
        msg.addBitString(txid);
}

}  // namespace ripple
