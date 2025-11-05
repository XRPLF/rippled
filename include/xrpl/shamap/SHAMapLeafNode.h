#ifndef XRPL_SHAMAP_SHAMAPLEAFNODE_H_INCLUDED
#define XRPL_SHAMAP_SHAMAPLEAFNODE_H_INCLUDED

#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <cstdint>

namespace ripple {

class SHAMapLeafNode : public SHAMapTreeNode
{
protected:
    boost::intrusive_ptr<SHAMapItem const> item_;

    SHAMapLeafNode(
        boost::intrusive_ptr<SHAMapItem const> item,
        std::uint32_t cowid);

    SHAMapLeafNode(
        boost::intrusive_ptr<SHAMapItem const> item,
        std::uint32_t cowid,
        SHAMapHash const& hash);

public:
    SHAMapLeafNode(SHAMapLeafNode const&) = delete;
    SHAMapLeafNode&
    operator=(SHAMapLeafNode const&) = delete;

    bool
    isLeaf() const final override
    {
        return true;
    }

    bool
    isInner() const final override
    {
        return false;
    }

    void
    invariants(bool is_root = false) const final override;

public:
    boost::intrusive_ptr<SHAMapItem const> const&
    peekItem() const;

    /** Set the item that this node points to and update the node's hash.

        @param i the new item
        @return false if the change was, effectively, a noop (that is, if the
                hash was unchanged); true otherwise.
     */
    bool
    setItem(boost::intrusive_ptr<SHAMapItem const> i);

    std::string
    getString(SHAMapNodeID const&) const final override;
};

}  // namespace ripple

#endif
