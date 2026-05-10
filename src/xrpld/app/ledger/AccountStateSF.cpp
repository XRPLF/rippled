#include <xrpld/app/ledger/AccountStateSF.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/shamap/SHAMapTreeNode.h>
#include <xrpl/basics/TraceLog.h>

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
    TRACE_FUNC();
    db_.store(NodeObjectType::AccountNode, std::move(nodeData), nodeHash.asUint256(), ledgerSeq);
}

std::optional<Blob>
AccountStateSF::getNode(SHAMapHash const& nodeHash) const
{
    TRACE_FUNC();
    return fp_.getFetchPack(nodeHash.asUint256());
}

}  // namespace xrpl
