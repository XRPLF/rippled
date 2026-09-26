#pragma once

#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <cstdint>
#include <string>

namespace xrpl {

class SHAMapLeafNode : public SHAMapTreeNode
{
protected:
    boost::intrusive_ptr<SHAMapItem const> item_;

    SHAMapLeafNode(boost::intrusive_ptr<SHAMapItem const> item, std::uint32_t cowid);

    SHAMapLeafNode(
        boost::intrusive_ptr<SHAMapItem const> item,
        std::uint32_t cowid,
        SHAMapHash const& hash);

public:
    SHAMapLeafNode(SHAMapLeafNode const&) = delete;
    SHAMapLeafNode&
    operator=(SHAMapLeafNode const&) = delete;

    bool
    isLeaf() const final
    {
        return true;
    }

    bool
    isInner() const final
    {
        return false;
    }

    void
    invariants(bool isRoot = false) const final;

public:
    boost::intrusive_ptr<SHAMapItem const> const&
    peekItem() const;

    /**
     * Set the item that this node points to and update the node's hash.
     *
     * @param i the new item
     * @return false if the change was, effectively, a noop (that is, if the
     *         hash was unchanged); true otherwise.
     */
    bool
    setItem(boost::intrusive_ptr<SHAMapItem const> i);

    std::string
    getString(SHAMapNodeID const&) const final;
};

/**
 * Return the key of the item held by a SHAMap leaf node.
 *
 * @param node a node known to be a leaf (see SHAMapTreeNode::isLeaf).
 */
inline uint256 const&
leafKey(SHAMapTreeNode const& node)
{
    XRPL_ASSERT(node.isLeaf(), "xrpl::leafKey : node is a leaf");
    return safeDowncast<SHAMapLeafNode const&>(node).peekItem()->key();
}

/**
 * Whether a node may occupy a position in a SHAMap.
 *
 * A leaf's own key names its position, so an ID that is not a prefix of that
 * key names a different subtree than the one the leaf belongs to. An inner
 * node carries no key, so every position is consistent with it and the
 * caller's own depth rules are what bound it.
 *
 * @param nodeID the position the node is claimed to occupy.
 * @param node the node to judge.
 * @return whether the node's own key agrees with that position.
 */
[[nodiscard]] inline bool
belongsAt(SHAMapNodeID const& nodeID, SHAMapTreeNode const& node)
{
    return !node.isLeaf() || nodeID.isPrefixOf(leafKey(node));
}

}  // namespace xrpl
