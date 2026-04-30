#include <xrpld/app/ledger/AccountStateSF.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace xrpl {

void
AccountStateSF::gotNode(
    bool,
    SHAMapHash const& nodeHash,
    std::uint32_t ledgerSeq,
    Blob&& nodeData,
    SHAMapNodeType) const
{
    db_.store(
        NodeObjectType::hotACCOUNT_NODE, std::move(nodeData), nodeHash.as_uint256(), ledgerSeq);
}

std::optional<Blob>
AccountStateSF::getNode(SHAMapHash const& nodeHash) const
{
    return fp_.getFetchPack(nodeHash.as_uint256());
}

}  // namespace xrpl
