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

#include <bitset>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace ripple {

class SHAMapInnerNode final : public SHAMapTreeNode,
                              public CountedObject<SHAMapInnerNode>
{
    std::array<SHAMapHash, 16> mHashes;
    std::shared_ptr<SHAMapTreeNode> mChildren[16];
    int mIsBranch = 0;
    std::uint32_t mFullBelowGen = 0;

    static std::mutex childLock;

public:
    SHAMapInnerNode(std::uint32_t cowid);

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
    setChild(int m, std::shared_ptr<SHAMapTreeNode> const& child);
    void
    shareChild(int m, std::shared_ptr<SHAMapTreeNode> const& child);
    SHAMapTreeNode*
    getChildPointer(int branch);
    std::shared_ptr<SHAMapTreeNode>
    getChild(int branch);
    virtual std::shared_ptr<SHAMapTreeNode>
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

inline SHAMapInnerNode::SHAMapInnerNode(std::uint32_t cowid)
    : SHAMapTreeNode(cowid)
{
}

inline bool
SHAMapInnerNode::isEmptyBranch(int m) const
{
    return (mIsBranch & (1 << m)) == 0;
}

inline SHAMapHash const&
SHAMapInnerNode::getChildHash(int m) const
{
    assert(m >= 0 && m < 16);
    return mHashes[m];
}

inline bool
SHAMapInnerNode::isFullBelow(std::uint32_t generation) const
{
    return mFullBelowGen == generation;
}

inline void
SHAMapInnerNode::setFullBelowGen(std::uint32_t gen)
{
    mFullBelowGen = gen;
}

}  // namespace ripple
#endif
