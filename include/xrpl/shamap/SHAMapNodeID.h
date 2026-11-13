#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/base_uint.h>

#include <compare>
#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>

namespace xrpl {

/**
 * Identifies a node inside a SHAMap
 */
class SHAMapNodeID : public CountedObject<SHAMapNodeID>
{
private:
    UInt256 id_;
    unsigned int depth_ = 0;

public:
    SHAMapNodeID() = default;
    SHAMapNodeID(SHAMapNodeID const& other) = default;
    SHAMapNodeID(unsigned int depth, UInt256 const& hash);

    SHAMapNodeID&
    operator=(SHAMapNodeID const& other) = default;

    [[nodiscard]] bool
    isRoot() const
    {
        return depth_ == 0;
    }

    // Get the wire format (256-bit nodeID, 1-byte depth)
    [[nodiscard]] std::string
    getRawString() const;

    [[nodiscard]] unsigned int
    getDepth() const
    {
        return depth_;
    }

    [[nodiscard]] UInt256 const&
    getNodeID() const
    {
        return id_;
    }

    [[nodiscard]] SHAMapNodeID
    getChildNodeID(unsigned int branch) const;

    /**
     * Test whether this node ID lies on the path to the given leaf key
     *
     * A node at depth d identifies the tree path spelled by the first d
     * nibbles of its key, so any leaf beneath it must agree on that prefix.
     * A node ID that fails this test names a different subtree than the one
     * it was built for.
     *
     * @param key  the key of a leaf below this node
     * @return whether this node ID is a prefix of the leaf key
     */
    [[nodiscard]] bool
    isPrefixOf(UInt256 const& key) const;

    /**
     * Create a SHAMapNodeID of a node with the depth of the node and
     * the key of a leaf
     *
     * @param depth  the depth of the node
     * @param key  the key of a leaf
     * @return SHAMapNodeID of the node
     */
    static SHAMapNodeID
    createID(unsigned int depth, UInt256 const& key);

    /**
     * Comparison operators
     *
     * <, >, <= and >= are synthesized from the spaceship. It is written out
     * rather than defaulted because the ordering is by depth first, and the
     * members are not declared in that order.
     */
    std::strong_ordering
    operator<=>(SHAMapNodeID const& n) const
    {
        return std::tie(depth_, id_) <=> std::tie(n.depth_, n.id_);
    }

    /**
     * Equality, which the spaceship above does not provide.
     *
     * Only a *defaulted* operator<=> implicitly declares a defaulted
     * operator==; the one above is user-provided, so == has to be written.
     * It cannot be defaulted either, because a defaulted == would also compare
     * the CountedObject base, which is not equality comparable.
     */
    bool
    operator==(SHAMapNodeID const& n) const
    {
        return (depth_ == n.depth_) && (id_ == n.id_);
    }
};

inline std::string
to_string(SHAMapNodeID const& node)
{
    if (node.isRoot())
        return "NodeID(root)";

    return "NodeID(" + std::to_string(node.getDepth()) + "," + to_string(node.getNodeID()) + ")";
}

inline std::ostream&
operator<<(std::ostream& out, SHAMapNodeID const& node)
{
    return out << to_string(node);
}

/**
 * Returns the id of the child a parent's branch leads to
 *
 * A child's id is its parent's with the nibble at the parent's own depth set to
 * the branch taken. Every other nibble is copied through, so a masked parent
 * gives a masked child. The write counterpart of selectBranch.
 *
 * @param parentID the id of the parent. The nibble at parentDepth is
 *                 overwritten rather than merged, so an id already carrying one
 *                 there is accepted.
 * @param parentDepth the depth of the parent, below SHAMap::kLeafDepth.
 * @param branch the branch of the parent leading to the child.
 * @return that child's id, whose depth is parentDepth + 1.
 */
[[nodiscard]] UInt256
childNodeID(UInt256 const& parentID, unsigned int parentDepth, unsigned int branch);

/**
 * Whether two keys name the same position at a given depth
 *
 * Both sides are masked, so either may carry bits below `depth`.
 * SHAMapNodeID::isPrefixOf is the stricter form.
 *
 * @param depth how many leading nibbles name the position, at most
 *              SHAMap::kLeafDepth. A greater depth is clamped to it.
 * @param lhs one of the keys.
 * @param rhs the other key.
 * @return whether the two agree on those nibbles.
 */
[[nodiscard]] bool
samePositionAtDepth(unsigned int depth, UInt256 const& lhs, UInt256 const& rhs);

/**
 * Return an object representing a serialized SHAMap Node ID
 *
 * @param s A string of bytes
 * @param data a non-null pointer to a buffer of @param size bytes.
 * @param size the size, in bytes, of the buffer pointed to by @param data.
 * @return A seated optional if the buffer contained a serialized SHAMap
 *         node ID and an unseated optional otherwise.
 */
/** @{ */
[[nodiscard]] std::optional<SHAMapNodeID>
deserializeSHAMapNodeID(void const* data, std::size_t size);

[[nodiscard]] inline std::optional<SHAMapNodeID>
deserializeSHAMapNodeID(std::string_view s)
{
    return deserializeSHAMapNodeID(s.data(), s.size());
}
/** @} */

/**
 * Returns the branch at the given depth that would contain the given hash
 *
 * @param depth the depth of the node whose branch to select.
 * @param hash the key whose nibble at that depth names the branch.
 * @return the branch containing the hash.
 */
[[nodiscard]] unsigned int
selectBranch(unsigned int depth, UInt256 const& hash);

/**
 * Returns the branch that would contain the given hash
 *
 * @param id the node whose depth to read.
 * @param hash the key whose nibble at that depth names the branch.
 * @return the branch containing the hash.
 */
[[nodiscard]] inline unsigned int
selectBranch(SHAMapNodeID const& id, UInt256 const& hash)
{
    return selectBranch(id.getDepth(), hash);
}

}  // namespace xrpl
