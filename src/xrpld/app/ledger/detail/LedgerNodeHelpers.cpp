#include <xrpld/app/ledger/detail/LedgerNodeHelpers.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapLeafNode.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <xrpl.pb.h>

#include <exception>
#include <optional>
#include <string_view>

namespace xrpl {

bool
validateLedgerNode(protocol::TMLedgerNode const& ledgerNode)
{
    if (!ledgerNode.has_nodedata())
        return false;

    if (ledgerNode.has_nodeid())
        return !ledgerNode.has_id() && !ledgerNode.has_depth();

    return ledgerNode.has_id() ||
        (ledgerNode.has_depth() && ledgerNode.depth() <= SHAMap::leafDepth);
}

SHAMapTreeNodePtr
getTreeNode(std::string_view data)
{
    auto const slice = makeSlice(data);
    try
    {
        return SHAMapTreeNode::makeFromWire(slice);
    }
    catch (std::exception const&)
    {
        return {};
    }
}

std::optional<SHAMapNodeID>
getSHAMapNodeID(protocol::TMLedgerNode const& ledgerNode, SHAMapTreeNodePtr const& treeNode)
{
    if (ledgerNode.has_id() || ledgerNode.has_depth())
    {
        if (treeNode->isInner())
        {
            if (!ledgerNode.has_id())
                return std::nullopt;

            return deserializeSHAMapNodeID(ledgerNode.id());
        }

        if (treeNode->isLeaf())
        {
            if (!ledgerNode.has_depth())
                return std::nullopt;

            auto const key =
                safe_downcast<SHAMapLeafNode const*>(treeNode.get())->peekItem()->key();
            return SHAMapNodeID::createID(ledgerNode.depth(), key);
        }

        UNREACHABLE("xrpl::getSHAMapNodeID : tree node is neither inner nor leaf");
        return std::nullopt;
    }

    if (!ledgerNode.has_nodeid())
        return std::nullopt;

    auto nodeID = deserializeSHAMapNodeID(ledgerNode.nodeid());
    if (!nodeID.has_value())
        return std::nullopt;

    if (treeNode->isLeaf())
    {
        auto const key = safe_downcast<SHAMapLeafNode const*>(treeNode.get())->peekItem()->key();
        auto const expected_id = SHAMapNodeID::createID(static_cast<int>(nodeID->getDepth()), key);
        if (nodeID->getNodeID() != expected_id.getNodeID())
            return std::nullopt;
    }

    return nodeID;
}

}  // namespace xrpl
