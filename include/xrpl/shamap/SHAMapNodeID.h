/** @file
 *  Defines `SHAMapNodeID`, the (depth, masked-prefix) address type used to
 *  locate nodes within a SHAMap 16-way radix-Merkle trie.
 *
 *  Every traversal, sync, and proof operation in the SHAMap subsystem
 *  carries a `SHAMapNodeID` alongside node pointers so the tree position
 *  is always unambiguous — independent of node content.
 */

#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/base_uint.h>

#include <optional>
#include <string>
#include <tuple>

namespace xrpl {

/** Encodes the position of a node within a SHAMap as a (depth, prefix) pair.
 *
 *  A SHAMap has 65 levels (depth 0 = root, depth 64 = leaf level). Each
 *  level consumes one nibble (4 bits) of the 256-bit key, giving a branch
 *  factor of 16.  `SHAMapNodeID` captures "which node" rather than "which
 *  data": two nodes with the same (depth, masked prefix) are at the same
 *  tree position regardless of their content or hash.
 *
 *  @invariant `id_` carries non-zero bits only in its top `depth_` nibbles;
 *      all lower nibbles are zero.  The constructor asserts this property.
 *      Use `createID()` when constructing from an unmasked leaf key.
 *
 *  Live instance counts are tracked via `CountedObject` for diagnostic
 *  purposes and can be queried through `CountedObjects::getInstance()`.
 */
class SHAMapNodeID : public CountedObject<SHAMapNodeID>
{
private:
    uint256 id_;
    unsigned int depth_ = 0;

public:
    SHAMapNodeID() = default;
    SHAMapNodeID(SHAMapNodeID const& other) = default;

    /** Construct a node ID at the given depth with a pre-masked prefix.
     *
     *  The caller is responsible for supplying a correctly masked `hash`:
     *  only the top `depth` nibbles may be non-zero.  Violating this
     *  invariant triggers an assertion.  Prefer `createID()` when
     *  constructing from an unmasked leaf key.
     *
     *  @param depth  Tree level, 0 (root) through `SHAMap::kLEAF_DEPTH` (64).
     *  @param hash   256-bit path prefix, masked to `depth` nibbles.
     */
    SHAMapNodeID(unsigned int depth, uint256 const& hash);

    SHAMapNodeID&
    operator=(SHAMapNodeID const& other) = default;

    [[nodiscard]] bool
    isRoot() const
    {
        return depth_ == 0;
    }

    /** Serialize this node ID to its 33-byte wire representation.
     *
     *  Produces a 33-byte string: the 32-byte big-endian `id_` prefix
     *  followed by a single byte containing `depth_`.  Used when
     *  exchanging node positions with peers during tree synchronization.
     *
     *  @return A 33-byte string containing the wire-format node ID.
     */
    [[nodiscard]] std::string
    getRawString() const;

    [[nodiscard]] unsigned int
    getDepth() const
    {
        return depth_;
    }

    [[nodiscard]] uint256 const&
    getNodeID() const
    {
        return id_;
    }

    /** Return the node ID of the child at branch `m`.
     *
     *  Increments `depth_` by one and sets the nibble at the new depth
     *  to `m`, producing the canonical address of that child in the tree.
     *  This is the step primitive used by every traversal loop in SHAMap.
     *
     *  @param m  Branch index, 0–15.
     *  @return   The child's `SHAMapNodeID` at `depth_ + 1`.
     *  @throws   `std::logic_error` if called on a leaf-depth node
     *            (`depth_ >= SHAMap::kLEAF_DEPTH`), because a depth-65
     *            node would violate the tree's structural invariant.
     */
    [[nodiscard]] SHAMapNodeID
    getChildNodeID(unsigned int m) const;

    /** Derive the ancestor node ID at `depth` from a full leaf key.
     *
     *  Applies the depth mask to `key`, discarding the lower nibbles,
     *  and constructs the resulting `SHAMapNodeID`.  This is the correct
     *  factory to use when you have a leaf key and need the address of an
     *  intermediate ancestor — for example, during sync validation where
     *  `addKnownNode` reconstructs the expected position of a received
     *  leaf and compares it against the claimed `SHAMapNodeID`.
     *
     *  @param depth  Tree level of the desired ancestor, 0–64.
     *  @param key    Full 256-bit leaf key; lower nibbles are masked away.
     *  @return       The `SHAMapNodeID` at `depth` on the path to `key`.
     */
    static SHAMapNodeID
    createID(int depth, uint256 const& key);

    // FIXME-C++20: use spaceship and operator synthesis

    /** Compare node IDs lexicographically by (depth, prefix).
     *
     *  Shallower nodes (smaller `depth_`) sort before deeper ones; among
     *  equal depths, ordering follows the 256-bit prefix value.  This
     *  ordering is used when storing node IDs in `std::map` or `std::set`
     *  containers that track traversal or sync frontiers.
     */
    bool
    operator<(SHAMapNodeID const& n) const
    {
        return std::tie(depth_, id_) < std::tie(n.depth_, n.id_);
    }

    /** @{ */
    bool
    operator>(SHAMapNodeID const& n) const
    {
        return n < *this;
    }

    bool
    operator<=(SHAMapNodeID const& n) const
    {
        return !(n < *this);
    }

    bool
    operator>=(SHAMapNodeID const& n) const
    {
        return !(*this < n);
    }

    bool
    operator==(SHAMapNodeID const& n) const
    {
        return (depth_ == n.depth_) && (id_ == n.id_);
    }

    bool
    operator!=(SHAMapNodeID const& n) const
    {
        return !(*this == n);
    }
    /** @} */
};

/** Format a node ID as a human-readable string for logging.
 *
 *  Returns `"NodeID(root)"` for the root node (depth 0) or
 *  `"NodeID(<depth>,<hex_id>)"` for all other nodes.  This format
 *  appears in journal messages throughout the SHAMap traversal code.
 *
 *  @param node  The node ID to format.
 *  @return      A human-readable string representation.
 */
inline std::string
to_string(SHAMapNodeID const& node)
{
    if (node.isRoot())
        return "NodeID(root)";

    return "NodeID(" + std::to_string(node.getDepth()) + "," + to_string(node.getNodeID()) + ")";
}

/** Write a node ID to an output stream using `to_string()` format. */
inline std::ostream&
operator<<(std::ostream& out, SHAMapNodeID const& node)
{
    return out << to_string(node);
}

/** Deserialize a `SHAMapNodeID` from a 33-byte wire buffer.
 *
 *  Validates that `size` is exactly 33, that the depth byte (at offset 32)
 *  does not exceed `SHAMap::kLEAF_DEPTH` (64), and that the 32-byte prefix
 *  satisfies the depth-mask invariant.  Any violation yields an empty
 *  optional rather than an exception, making this safe to call on
 *  untrusted peer data.
 *
 *  @param data  Pointer to the raw byte buffer.
 *  @param size  Length of the buffer in bytes; must equal 33 for success.
 *  @return      The decoded `SHAMapNodeID`, or an empty optional on any
 *      validation failure.
 */
/** @{ */
[[nodiscard]] std::optional<SHAMapNodeID>
deserializeSHAMapNodeID(void const* data, std::size_t size);

/** @cond */
[[nodiscard]] inline std::optional<SHAMapNodeID>
deserializeSHAMapNodeID(std::string const& s)
{
    return deserializeSHAMapNodeID(s.data(), s.size());
}
/** @endcond */
/** @} */

/** Return the branch index (0–15) to follow from `id` toward `hash`.
 *
 *  Extracts the nibble of `hash` at depth `id.getDepth()`, which is the
 *  child branch number to descend into at that level.  This is the core
 *  traversal primitive: every lookup, insert, delete, and sync walk in
 *  SHAMap calls `selectBranch` to advance one level down the trie.
 *
 *  @param id    Position of the current node; its depth determines which
 *      nibble of `hash` to read.
 *  @param hash  The 256-bit key being navigated toward.
 *  @return      Branch index in [0, 15].
 */
[[nodiscard]] unsigned int
selectBranch(SHAMapNodeID const& id, uint256 const& hash);

}  // namespace xrpl
