#include <xrpld/app/ledger/LedgerNodeHelpers.h>

#include <xrpl/basics/Slice.h>
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
getSHAMapNodeID(protocol::TMLedgerNode const& ledgerNode, SHAMapTreeNode const& treeNode)
{
    if (ledgerNode.has_id() || ledgerNode.has_depth())
    {
        // Reject ambiguous messages that mix the legacy and new reference fields.
        if (ledgerNode.has_nodeid())
            return std::nullopt;

        if (treeNode.isInner())
        {
            if (!ledgerNode.has_id())
                return std::nullopt;

            REACHABLE("xrpl::getSHAMapNodeID : inner node ID from id field");
            return deserializeSHAMapNodeID(ledgerNode.id());
        }

        if (treeNode.isLeaf())
        {
            SOMETIMES(
                ledgerNode.has_depth() && ledgerNode.depth() > SHAMap::kLeafDepth,
                "xrpl::getSHAMapNodeID : leaf depth exceeds max");
            if (!ledgerNode.has_depth() || ledgerNode.depth() > SHAMap::kLeafDepth)
                return std::nullopt;

            auto const key = leafKey(treeNode);
            REACHABLE("xrpl::getSHAMapNodeID : leaf node ID reconstructed from depth");
            return SHAMapNodeID::createID(ledgerNode.depth(), key);
        }
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::getSHAMapNodeID : tree node is neither inner nor leaf");
        return std::nullopt;
        // LCOV_EXCL_STOP
    }

    if (!ledgerNode.has_nodeid())
        return std::nullopt;

    auto nodeID = deserializeSHAMapNodeID(ledgerNode.nodeid());
    if (!nodeID.has_value())
        return std::nullopt;

    if (treeNode.isLeaf())
    {
        auto const key = leafKey(treeNode);
        auto const expectedID = SHAMapNodeID::createID(static_cast<int>(nodeID->getDepth()), key);
        SOMETIMES(
            nodeID->getNodeID() != expectedID.getNodeID(),
            "xrpl::getSHAMapNodeID : legacy leaf ID inconsistent with key");
        if (nodeID->getNodeID() != expectedID.getNodeID())
            return std::nullopt;
    }

    return nodeID;
}

}  // namespace xrpl
