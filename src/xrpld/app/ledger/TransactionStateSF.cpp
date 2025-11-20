#include <xrpld/app/ledger/TransactionStateSF.h>

namespace ripple {

void
TransactionStateSF::gotNode(
    bool,
    SHAMapHash const& nodeHash,
    std::uint32_t ledgerSeq,
    Blob&& nodeData,
    SHAMapNodeType type) const

{
    XRPL_ASSERT(
        type != SHAMapNodeType::tnTRANSACTION_NM,
        "ripple::TransactionStateSF::gotNode : valid input");
    db_.store(
        hotTRANSACTION_NODE,
        std::move(nodeData),
        nodeHash.as_uint256(),
        ledgerSeq);
}

std::optional<Blob>
TransactionStateSF::getNode(SHAMapHash const& nodeHash) const
{
    return fp_.getFetchPack(nodeHash.as_uint256());
}

}  // namespace ripple
