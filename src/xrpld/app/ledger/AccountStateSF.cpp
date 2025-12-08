#include <xrpld/app/ledger/AccountStateSF.h>

namespace ripple {

void
AccountStateSF::gotNode(
    bool,
    SHAMapHash const& nodeHash,
    std::uint32_t ledgerSeq,
    Blob&& nodeData,
    SHAMapNodeType) const
{
    db_.store(
        hotACCOUNT_NODE, std::move(nodeData), nodeHash.as_uint256(), ledgerSeq);
}

std::optional<Blob>
AccountStateSF::getNode(SHAMapHash const& nodeHash) const
{
    return fp_.getFetchPack(nodeHash.as_uint256());
}

}  // namespace ripple
