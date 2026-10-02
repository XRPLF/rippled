#pragma once

#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <optional>
#include <string_view>

namespace protocol {
class TMLedgerNode;
}  // namespace protocol

namespace xrpl {

/**
 * @brief Deserializes a SHAMapTreeNode from wire format data.
 *
 * This function attempts to create a SHAMapTreeNode from the provided data string. If the data is
 * malformed or deserialization fails, the function returns a nullptr instead of throwing an
 * exception.
 *
 * @param data The serialized node data in wire format.
 * @return The deserialized tree node if successful, or a nullptr if deserialization fails.
 */
[[nodiscard]] SHAMapTreeNodePtr
getTreeNode(std::string_view data);

/**
 * @brief Extracts or reconstructs the SHAMapNodeID from a ledger node proto message.
 *
 * This function retrieves the SHAMapNodeID for a tree node, with behavior that depends on which
 * field is set and the node type (inner vs. leaf).
 *
 * When the legacy `nodeid` field is set in the message:
 * - For all nodes: Deserializes the node ID from the field.
 * - For leaf nodes: Validates that the node ID is consistent with the leaf's key.
 *
 * When the new `id` or `depth` field is set in the message:
 * - For inner nodes: Deserializes the node ID from the `id` field.
 * - For leaf nodes: Reconstructs the node ID using both the depth from the `depth` field and the
 *   key from the leaf node's item.
 * Note that root nodes may be inner nodes or leaf nodes.
 *
 * @param ledgerNode The validated protocol message containing the ledger node data.
 * @param treeNode The deserialized tree node (inner or leaf node).
 * @return An optional containing the node ID if extraction/reconstruction succeeds, or std::nullopt
 *         if the required fields are missing or validation fails.
 */
[[nodiscard]] std::optional<SHAMapNodeID>
getSHAMapNodeID(protocol::TMLedgerNode const& ledgerNode, SHAMapTreeNode const& treeNode);

}  // namespace xrpl
