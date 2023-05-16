//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_SHAMAP_SHAMAPINNERNODE_H_INCLUDED
#define RIPPLE_SHAMAP_SHAMAPINNERNODE_H_INCLUDED

#include <ripple/basics/TaggedCache.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/shamap/SHAMapItem.h>
#include <ripple/shamap/SHAMapNodeID.h>
#include <ripple/shamap/SHAMapTreeNode.h>
#include <ripple/shamap/impl/TaggedPointer.h>

#include <atomic>
#include <bitset>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace ripple {

class SHAMapInnerNode final : public SHAMapTreeNode,
                              public CountedObject<SHAMapInnerNode>
{
public:
    /** Each inner node has 16 children (the 'radix tree' part of the map) */
    static inline constexpr unsigned int branchFactor = 16;

private:
    /** Opaque type that contains the `hashes` array (array of type
       `SHAMapHash`) and the `children` array (array of type
       `std::shared_ptr<SHAMapInnerNode>`).
     */
    TaggedPointer hashesAndChildren_;

    std::uint32_t fullBelowGen_ = 0;
    std::uint16_t isBranch_ = 0;

    /** A bitlock for the children of this node, with one bit per child */
    mutable std::atomic<std::uint16_t> lock_ = 0;

    /** Convert arrays stored in `hashesAndChildren_` so they can store the
        requested number of children.

        @param toAllocate allocate space for at least this number of children
        (must be <= branchFactor)

        @note the arrays may allocate more than the requested value in
        `toAllocate`. This is due to the implementation of TagPointer, which
        only supports allocating arrays of 4 different sizes.
     */
    void
    resizeChildArrays(std::uint8_t toAllocate);

    /** Get the child's index inside the `hashes` or `children` array (stored in
        `hashesAndChildren_`.

        These arrays may or may not be sparse). The optional will be empty is an
        empty branch is requested and the arrays are sparse.

        @param i index of the requested child
     */
    std::optional<int>
    getChildIndex(int i) const;

    /** Call the `f` callback for all 16 (branchFactor) branches - even if
        the branch is empty.

        @param f a one parameter callback function. The parameter is the
        child's hash.
    */
    template <class F>
    void
    iterChildren(F&& f) const;

    /** Call the `f` callback for all non-empty branches.

        @param f a two parameter callback function. The first parameter is
        the branch number, the second parameter is the index into the array.
        For dense formats these are the same, for sparse they may be
        different.
    */
    template <class F>
    void
    iterNonEmptyChildIndexes(F&& f) const;

public:
    explicit SHAMapInnerNode(
        std::uint32_t cowid,
        std::uint8_t numAllocatedChildren = 2);

    SHAMapInnerNode(SHAMapInnerNode const&) = delete;
    SHAMapInnerNode&
    operator=(SHAMapInnerNode const&) = delete;
    ~SHAMapInnerNode();

    std::shared_ptr<SHAMapTreeNode>
    clone(std::uint32_t cowid) const override;

    SHAMapNodeType
    getType() const override
    {
        return SHAMapNodeType::tnINNER;
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
    isEmptyBranch(int m) const;

    int
    getBranchCount() const;

    SHAMapHash const&
    getChildHash(int m) const;

    void
    setChild(int m, std::shared_ptr<SHAMapTreeNode> child);

    void
    shareChild(int m, std::shared_ptr<SHAMapTreeNode> const& child);

    SHAMapTreeNode*
    getChildPointer(int branch);

    std::shared_ptr<SHAMapTreeNode>
    getChild(int branch);

    std::shared_ptr<SHAMapTreeNode>
    canonicalizeChild(int branch, std::shared_ptr<SHAMapTreeNode> node);

    // sync functions
    bool
    isFullBelow(std::uint32_t generation) const;

    void
    setFullBelowGen(std::uint32_t gen);

    void
    updateHash() override;

    /** Recalculate the hash of all children and this node. */
    void
    updateHashDeep();

    void
    serializeForWire(Serializer&) const override;

    void
    serializeWithPrefix(Serializer&) const override;

    std::string
    getString(SHAMapNodeID const&) const override;

    void
    invariants(bool is_root = false) const override;

    static std::shared_ptr<SHAMapTreeNode>
    makeFullInner(Slice data, SHAMapHash const& hash, bool hashValid);

    static std::shared_ptr<SHAMapTreeNode>
    makeCompressedInner(Slice data);
};

inline bool
SHAMapInnerNode::isEmptyBranch(int m) const
{
    return (isBranch_ & (1 << m)) == 0;
}

inline bool
SHAMapInnerNode::isFullBelow(std::uint32_t generation) const
{
    return fullBelowGen_ == generation;
}

inline void
SHAMapInnerNode::setFullBelowGen(std::uint32_t gen)
{
    fullBelowGen_ = gen;
}

}  // namespace ripple
#endif
