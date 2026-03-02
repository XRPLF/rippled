#include <xrpld/app/ledger/detail/LedgerNodeHelpers.h>

#include <xrpl/basics/IntrusivePointer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/messages.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapLeafNode.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <optional>
#include <string>

namespace xrpl {

bool
validateLedgerNode(protocol::TMLedgerNode const& ledger_node)
{
    if (!ledger_node.has_nodedata())
        return false;

    if (ledger_node.has_nodeid())
        return !ledger_node.has_id() && !ledger_node.has_depth();

    return ledger_node.has_id() ||
        (ledger_node.has_depth() && ledger_node.depth() <= SHAMap::leafDepth);
}

std::optional<intr_ptr::SharedPtr<SHAMapTreeNode>>
getTreeNode(std::string_view data)
{
    auto const slice = makeSlice(data);
    try
    {
        auto treeNode = SHAMapTreeNode::makeFromWire(slice);
        if (!treeNode)
            return std::nullopt;
        return treeNode;
    }
    catch (std::exception const&)
    {
        return std::nullopt;
    }
}

std::optional<SHAMapNodeID>
getSHAMapNodeID(
    protocol::TMLedgerNode const& ledger_node,
    intr_ptr::SharedPtr<SHAMapTreeNode> const& treeNode)
{
    if (ledger_node.has_id() || ledger_node.has_depth())
    {
        if (treeNode->isInner())
        {
            if (!ledger_node.has_id())
                return std::nullopt;

            return deserializeSHAMapNodeID(ledger_node.id());
        }

        if (treeNode->isLeaf())
        {
            if (!ledger_node.has_depth())
                return std::nullopt;

            auto const key = static_cast<SHAMapLeafNode const*>(treeNode.get())->peekItem()->key();
            return SHAMapNodeID::createID(ledger_node.depth(), key);
        }

        UNREACHABLE("xrpl::getSHAMapNodeID : tree node is neither inner nor leaf");
        return std::nullopt;
    }

    if (!ledger_node.has_nodeid())
        return std::nullopt;

    auto const nodeID = deserializeSHAMapNodeID(ledger_node.nodeid());
    if (!nodeID)
        return std::nullopt;

    if (treeNode->isLeaf())
    {
        auto const key = static_cast<SHAMapLeafNode const*>(treeNode.get())->peekItem()->key();
        auto const expected_id = SHAMapNodeID::createID(static_cast<int>(nodeID->getDepth()), key);
        if (nodeID->getNodeID() != expected_id.getNodeID())
            return std::nullopt;
    }

    return nodeID;
}

}  // namespace xrpl
