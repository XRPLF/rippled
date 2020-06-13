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

#ifndef RIPPLE_SHAMAP_SHAMAPTREENODE_H_INCLUDED
#define RIPPLE_SHAMAP_SHAMAPTREENODE_H_INCLUDED

#include <ripple/basics/TaggedCache.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/shamap/SHAMapItem.h>
#include <ripple/shamap/SHAMapNodeID.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace ripple {

enum SHANodeFormat {
    snfPREFIX = 1,  // Form that hashes to its official hash
    snfWIRE = 2,    // Compressed form used on the wire
    snfHASH = 3,    // just the hash
};

// A SHAMapHash is the hash of a node in a SHAMap, and also the
// type of the hash of the entire SHAMap.
class SHAMapHash
{
    uint256 hash_;

public:
    SHAMapHash() = default;
    explicit SHAMapHash(uint256 const& hash) : hash_(hash)
    {
    }

    uint256 const&
    as_uint256() const
    {
        return hash_;
    }
    uint256&
    as_uint256()
    {
        return hash_;
    }
    bool
    isZero() const
    {
        return hash_.isZero();
    }
    bool
    isNonZero() const
    {
        return hash_.isNonZero();
    }
    int
    signum() const
    {
        return hash_.signum();
    }
    void
    zero()
    {
        hash_.zero();
    }

    friend bool
    operator==(SHAMapHash const& x, SHAMapHash const& y)
    {
        return x.hash_ == y.hash_;
    }

    friend bool
    operator<(SHAMapHash const& x, SHAMapHash const& y)
    {
        return x.hash_ < y.hash_;
    }

    friend std::ostream&
    operator<<(std::ostream& os, SHAMapHash const& x)
    {
        return os << x.hash_;
    }

    friend std::string
    to_string(SHAMapHash const& x)
    {
        return to_string(x.hash_);
    }

    template <class H>
    friend void
    hash_append(H& h, SHAMapHash const& x)
    {
        hash_append(h, x.hash_);
    }
};

inline bool
operator!=(SHAMapHash const& x, SHAMapHash const& y)
{
    return !(x == y);
}

class SHAMapAbstractNode
{
public:
    enum TNType {
        tnERROR = 0,
        tnINNER = 1,
        tnTRANSACTION_NM = 2,  // transaction, no metadata
        tnTRANSACTION_MD = 3,  // transaction, with metadata
        tnACCOUNT_STATE = 4
    };

protected:
    TNType mType;
    SHAMapHash mHash;
    std::uint32_t mSeq;

protected:
    virtual ~SHAMapAbstractNode() = 0;
    SHAMapAbstractNode(SHAMapAbstractNode const&) = delete;
    SHAMapAbstractNode&
    operator=(SHAMapAbstractNode const&) = delete;

    SHAMapAbstractNode(TNType type, std::uint32_t seq);
    SHAMapAbstractNode(TNType type, std::uint32_t seq, SHAMapHash const& hash);

public:
    std::uint32_t
    getSeq() const;
    void
    setSeq(std::uint32_t s);
    SHAMapHash const&
    getNodeHash() const;
    TNType
    getType() const;
    bool
    isLeaf() const;
    bool
    isInner() const;
    bool
    isValid() const;
    bool
    isInBounds(SHAMapNodeID const& id) const;

    virtual bool
    updateHash() = 0;
    virtual void
    addRaw(Serializer&, SHANodeFormat format) const = 0;
    virtual std::string
    getString(SHAMapNodeID const&) const;
    virtual std::shared_ptr<SHAMapAbstractNode>
    clone(std::uint32_t seq) const = 0;
    virtual uint256 const&
    key() const = 0;
    virtual void
    invariants(bool is_root = false) const = 0;

    static std::shared_ptr<SHAMapAbstractNode>
    makeFromPrefix(Slice rawNode, SHAMapHash const& hash);

    static std::shared_ptr<SHAMapAbstractNode>
    makeFromWire(Slice rawNode, SHAMapNodeID const& id);

private:
    static std::shared_ptr<SHAMapAbstractNode>
    makeTransaction(
        Slice data,
        std::uint32_t seq,
        SHAMapHash const& hash,
        bool hashValid);

    static std::shared_ptr<SHAMapAbstractNode>
    makeAccountState(
        Slice data,
        std::uint32_t seq,
        SHAMapHash const& hash,
        bool hashValid);

    static std::shared_ptr<SHAMapAbstractNode>
    makeTransactionWithMeta(
        Slice data,
        std::uint32_t seq,
        SHAMapHash const& hash,
        bool hashValid);
};

class SHAMapInnerNode : public SHAMapAbstractNode
{
    std::array<SHAMapHash, 16> mHashes;
    std::shared_ptr<SHAMapAbstractNode> mChildren[16];
    int mIsBranch = 0;
    std::uint32_t mFullBelowGen = 0;

    static std::mutex childLock;

public:
    SHAMapInnerNode(std::uint32_t seq);
    std::shared_ptr<SHAMapAbstractNode>
    clone(std::uint32_t seq) const override;

    bool
    isEmpty() const;
    bool
    isEmptyBranch(int m) const;
    int
    getBranchCount() const;
    SHAMapHash const&
    getChildHash(int m) const;

    void
    setChild(int m, std::shared_ptr<SHAMapAbstractNode> const& child);
    void
    shareChild(int m, std::shared_ptr<SHAMapAbstractNode> const& child);
    SHAMapAbstractNode*
    getChildPointer(int branch);
    std::shared_ptr<SHAMapAbstractNode>
    getChild(int branch);
    virtual std::shared_ptr<SHAMapAbstractNode>
    canonicalizeChild(int branch, std::shared_ptr<SHAMapAbstractNode> node);

    // sync functions
    bool
    isFullBelow(std::uint32_t generation) const;
    void
    setFullBelowGen(std::uint32_t gen);

    bool
    updateHash() override;
    void
    updateHashDeep();
    void
    addRaw(Serializer&, SHANodeFormat format) const override;
    std::string
    getString(SHAMapNodeID const&) const override;
    uint256 const&
    key() const override;
    void
    invariants(bool is_root = false) const override;

    static std::shared_ptr<SHAMapAbstractNode>
    makeFullInner(
        Slice data,
        std::uint32_t seq,
        SHAMapHash const& hash,
        bool hashValid);

    static std::shared_ptr<SHAMapAbstractNode>
    makeCompressedInner(Slice data, std::uint32_t seq);
};

// SHAMapTreeNode represents a leaf, and may eventually be renamed to reflect
// that.
class SHAMapTreeNode : public SHAMapAbstractNode
{
private:
    std::shared_ptr<SHAMapItem const> mItem;

public:
    SHAMapTreeNode(const SHAMapTreeNode&) = delete;
    SHAMapTreeNode&
    operator=(const SHAMapTreeNode&) = delete;

    SHAMapTreeNode(
        std::shared_ptr<SHAMapItem const> item,
        TNType type,
        std::uint32_t seq);
    SHAMapTreeNode(
        std::shared_ptr<SHAMapItem const> item,
        TNType type,
        std::uint32_t seq,
        SHAMapHash const& hash);
    std::shared_ptr<SHAMapAbstractNode>
    clone(std::uint32_t seq) const override;

    void
    addRaw(Serializer&, SHANodeFormat format) const override;
    uint256 const&
    key() const override;
    void
    invariants(bool is_root = false) const override;

public:  // public only to SHAMap
    // item node function
    bool
    hasItem() const;
    std::shared_ptr<SHAMapItem const> const&
    peekItem() const;
    bool
    setItem(std::shared_ptr<SHAMapItem const> i, TNType type);

    std::string
    getString(SHAMapNodeID const&) const override;
    bool
    updateHash() override;
};

// SHAMapAbstractNode

inline SHAMapAbstractNode::SHAMapAbstractNode(TNType type, std::uint32_t seq)
    : mType(type), mSeq(seq)
{
}

inline SHAMapAbstractNode::SHAMapAbstractNode(
    TNType type,
    std::uint32_t seq,
    SHAMapHash const& hash)
    : mType(type), mHash(hash), mSeq(seq)
{
}

inline std::uint32_t
SHAMapAbstractNode::getSeq() const
{
    return mSeq;
}

inline void
SHAMapAbstractNode::setSeq(std::uint32_t s)
{
    mSeq = s;
}

inline SHAMapHash const&
SHAMapAbstractNode::getNodeHash() const
{
    return mHash;
}

inline SHAMapAbstractNode::TNType
SHAMapAbstractNode::getType() const
{
    return mType;
}

inline bool
SHAMapAbstractNode::isLeaf() const
{
    return (mType == tnTRANSACTION_NM) || (mType == tnTRANSACTION_MD) ||
        (mType == tnACCOUNT_STATE);
}

inline bool
SHAMapAbstractNode::isInner() const
{
    return mType == tnINNER;
}

inline bool
SHAMapAbstractNode::isValid() const
{
    return mType != tnERROR;
}

inline bool
SHAMapAbstractNode::isInBounds(SHAMapNodeID const& id) const
{
    // Nodes at depth 64 must be leaves
    return (!isInner() || (id.getDepth() < 64));
}

// SHAMapInnerNode

inline SHAMapInnerNode::SHAMapInnerNode(std::uint32_t seq)
    : SHAMapAbstractNode(tnINNER, seq)
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
    assert((m >= 0) && (m < 16) && (getType() == tnINNER));
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

// SHAMapTreeNode

inline bool
SHAMapTreeNode::hasItem() const
{
    return bool(mItem);
}

inline std::shared_ptr<SHAMapItem const> const&
SHAMapTreeNode::peekItem() const
{
    return mItem;
}

}  // namespace ripple

#endif
