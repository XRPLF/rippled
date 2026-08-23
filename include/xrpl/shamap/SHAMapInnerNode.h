#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapTreeNode.h>
#include <xrpl/shamap/detail/TaggedPointer.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

namespace xrpl {

class SHAMapInnerNode final : public SHAMapTreeNode, public CountedObject<SHAMapInnerNode>
{
public:
    /**
     * Each inner node has 16 children (the 'radix tree' part of the map)
     */
    static constexpr unsigned int kBranchFactor = 16;

private:
    /**
     * Opaque type that contains the `hashes` array (array of type
     * `SHAMapHash`) and the `children` array (array of type
     * `intr_ptr::SharedPtr<SHAMapInnerNode>`).
     */
    TaggedPointer hashesAndChildren_;

    // Inner nodes are allocated in the millions into a deliberately packed layout, so pin that
    // wrapping fullBelowGen_ leaves every member at the offset it had, and that isFullBelow() does
    // not take a mutex once per node of every walk.
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(sizeof(std::atomic<std::uint32_t>) == sizeof(std::uint32_t));
    static_assert(alignof(std::atomic<std::uint32_t>) == alignof(std::uint32_t));

    /**
     * Written from more than one thread: canonicalization shares nodes between
     * maps, so concurrent walks of different maps reach the same node, and a
     * single map's walk can run with the acquisition lock released (see
     * SHAMap::state_).
     *
     * Relaxed both ways: a generation is only ever compared for equality
     * and publishes nothing, since the children it vouches for are
     * published through lock_. Ordering it would cost a barrier per inner
     * node of every walk.
     */
    std::atomic<std::uint32_t> fullBelowGen_ = 0;
    std::uint16_t isBranch_ = 0;

    /**
     * A bitlock for the children of this node, with one bit per child
     */
    mutable std::atomic<std::uint16_t> lock_ = 0;

    /**
     * Convert arrays stored in `hashesAndChildren_` so they can store the
     * requested number of children.
     *
     * @param toAllocate allocate space for at least this number of children
     * (must be <= branchFactor)
     *
     * @note the arrays may allocate more than the requested value in
     * `toAllocate`. This is due to the implementation of TagPointer, which
     * only supports allocating arrays of 4 different sizes.
     */
    void
    resizeChildArrays(std::uint8_t toAllocate);

    /**
     * Get the child's index inside the `hashes` or `children` array (stored in
     * `hashesAndChildren_`.
     *
     * These arrays may or may not be sparse). The optional will be empty is an
     * empty branch is requested and the arrays are sparse.
     *
     * @param i index of the requested child
     */
    std::optional<unsigned int>
    getChildIndex(unsigned int i) const;

    /**
     * Call the `f` callback for all 16 (branchFactor) branches - even if
     * the branch is empty.
     *
     * @param f a one parameter callback function. The parameter is the
     * child's hash.
     */
    template <class F>
    void
    iterChildren(F&& f) const;

    /**
     * Call the `f` callback for all non-empty branches.
     *
     * @param f a two parameter callback function. The first parameter is
     * the branch number, the second parameter is the index into the array.
     * For dense formats these are the same, for sparse they may be
     * different.
     */
    template <class F>
    void
    iterNonEmptyChildIndexes(F&& f) const;

public:
    explicit SHAMapInnerNode(std::uint32_t cowid, std::uint8_t numAllocatedChildren = 2);

    SHAMapInnerNode(SHAMapInnerNode const&) = delete;
    SHAMapInnerNode&
    operator=(SHAMapInnerNode const&) = delete;
    ~SHAMapInnerNode() override;

    // Needed to support intrusive weak pointers
    void
    partialDestructor() override;

    SHAMapTreeNodePtr
    clone(std::uint32_t cowid) const override;

    SHAMapNodeType
    getType() const override
    {
        return SHAMapNodeType::TnInner;
    }

    bool
    isLeaf() const override
    {
        return false;
    }

    bool
    isInner() const override
    {
        return true;
    }

    bool
    isEmpty() const;

    bool
    isEmptyBranch(unsigned int branch) const;

    unsigned int
    getBranchCount() const;

    SHAMapHash const&
    getChildHash(unsigned int branch) const;

    void
    setChild(unsigned int branch, SHAMapTreeNodePtr child);

    void
    shareChild(unsigned int branch, SHAMapTreeNodePtr const& child);

    SHAMapTreeNode*
    getChildPointer(unsigned int branch);

    SHAMapTreeNodePtr
    getChild(unsigned int branch);

    SHAMapTreeNodePtr
    canonicalizeChild(unsigned int branch, SHAMapTreeNodePtr node);

    // sync functions
    bool
    isFullBelow(std::uint32_t generation) const;

    void
    setFullBelowGen(std::uint32_t gen);

    void
    updateHash() override;

    /**
     * Recalculate the hash of all children and this node.
     */
    void
    updateHashDeep();

    void
    serializeForWire(Serializer&) const override;

    void
    serializeWithPrefix(Serializer&) const override;

    std::string
    getString(SHAMapNodeID const&) const override;

    void
    invariants(bool isRoot = false) const override;

    static SHAMapTreeNodePtr
    makeFullInner(Slice data, SHAMapHash const& hash, bool hashValid);

    static SHAMapTreeNodePtr
    makeCompressedInner(Slice data);
};

inline bool
SHAMapInnerNode::isEmpty() const
{
    return isBranch_ == 0;
}

inline bool
SHAMapInnerNode::isEmptyBranch(unsigned int branch) const
{
    return (isBranch_ & (1u << branch)) == 0u;
}

inline unsigned int
SHAMapInnerNode::getBranchCount() const
{
    return popcnt16(isBranch_);
}

inline bool
SHAMapInnerNode::isFullBelow(std::uint32_t generation) const
{
    return fullBelowGen_.load(std::memory_order_relaxed) == generation;
}

inline void
SHAMapInnerNode::setFullBelowGen(std::uint32_t gen)
{
    fullBelowGen_.store(gen, std::memory_order_relaxed);
}

}  // namespace xrpl
