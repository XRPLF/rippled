#include <xrpld/app/ledger/TransactionStateSF.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace xrpl {

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
        "xrpl::TransactionStateSF::gotNode : valid input");
    db_.store(
        NodeObjectType::hotTRANSACTION_NODE, std::move(nodeData), nodeHash.as_uint256(), ledgerSeq);
}

std::optional<Blob>
TransactionStateSF::getNode(SHAMapHash const& nodeHash) const
{
    return fp_.getFetchPack(nodeHash.as_uint256());
}

}  // namespace xrpl
