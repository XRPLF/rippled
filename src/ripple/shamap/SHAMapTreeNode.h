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

#include <ripple/shamap/SHAMapItem.h>
#include <ripple/shamap/SHAMapNodeID.h>
#include <ripple/basics/TaggedCache.h>
#include <beast/utility/Journal.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace ripple {

enum SHANodeFormat
{
    snfPREFIX   = 1, // Form that hashes to its official hash
    snfWIRE     = 2, // Compressed form used on the wire
    snfHASH     = 3, // just the hash
};

class SHAMapTreeNode
{
public:
    enum TNType
    {
        tnERROR             = 0,
        tnINNER             = 1,
        tnTRANSACTION_NM    = 2, // transaction, no metadata
        tnTRANSACTION_MD    = 3, // transaction, with metadata
        tnACCOUNT_STATE     = 4
    };

private:
    uint256                         mHash;
    uint256                         mHashes[16];
    std::shared_ptr<SHAMapTreeNode> mChildren[16];
    std::shared_ptr<SHAMapItem>     mItem;
    std::uint32_t                   mSeq;
    TNType                          mType;
    int                             mIsBranch;
    std::uint32_t                   mFullBelowGen;

    static std::mutex               childLock;

public:
    SHAMapTreeNode (const SHAMapTreeNode&) = delete;
    SHAMapTreeNode& operator= (const SHAMapTreeNode&) = delete;

    SHAMapTreeNode (std::uint32_t seq); // empty node
    SHAMapTreeNode (const SHAMapTreeNode & node, std::uint32_t seq); // copy node from older tree
    SHAMapTreeNode (std::shared_ptr<SHAMapItem> const& item, TNType type, std::uint32_t seq);
    SHAMapTreeNode (Blob const & data, std::uint32_t seq,
                    SHANodeFormat format, uint256 const& hash, bool hashValid);

    void addRaw (Serializer&, SHANodeFormat format);
    uint256 const& getNodeHash () const;

public:  // public only to SHAMap
    void setChild (int m, std::shared_ptr<SHAMapTreeNode> const& child);
    void shareChild (int m, std::shared_ptr<SHAMapTreeNode> const& child);

    // node functions
    std::uint32_t getSeq () const;
    void setSeq (std::uint32_t s);
    TNType getType () const;

    // type functions
    bool isLeaf () const;
    bool isInner () const;
    bool isInBounds (SHAMapNodeID const &id) const;
    bool isValid () const;

    // inner node functions
    bool isInnerNode () const;
    bool isEmptyBranch (int m) const;
    bool isEmpty () const;
    int getBranchCount () const;
    void makeInner ();
    uint256 const& getChildHash (int m) const;

    // item node function
    bool hasItem () const;
    std::shared_ptr<SHAMapItem> const& peekItem () const;
    bool setItem (std::shared_ptr<SHAMapItem> const& i, TNType type);

    // sync functions
    bool isFullBelow (std::uint32_t generation) const;
    void setFullBelowGen (std::uint32_t gen);

    SHAMapTreeNode* getChildPointer (int branch);
    std::shared_ptr<SHAMapTreeNode> getChild (int branch);
    void canonicalizeChild (int branch, std::shared_ptr<SHAMapTreeNode>& node);

    // debugging
#ifdef BEAST_DEBUG
    void dump (SHAMapNodeID const&, beast::Journal journal);
#endif
    std::string getString (SHAMapNodeID const&) const;
    bool updateHash ();
    void updateHashDeep();

private:
    bool isTransaction () const;
    bool hasMetaData () const;
    bool isAccountState () const;
};

inline
std::uint32_t
SHAMapTreeNode::getSeq () const
{
    return mSeq;
}

inline
void
SHAMapTreeNode::setSeq (std::uint32_t s)
{
    mSeq = s;
}

inline
uint256 const&
SHAMapTreeNode::getNodeHash () const
{
    return mHash;
}

inline
SHAMapTreeNode::TNType
SHAMapTreeNode::getType () const
{
    return mType;
}

inline
bool
SHAMapTreeNode::isLeaf () const
{
    return (mType == tnTRANSACTION_NM) || (mType == tnTRANSACTION_MD) ||
           (mType == tnACCOUNT_STATE);
}

inline
bool
SHAMapTreeNode::isInner () const
{
    return mType == tnINNER;
}

inline
bool
SHAMapTreeNode::isInBounds (SHAMapNodeID const &id) const
{
    // Nodes at depth 64 must be leaves
    return (!isInner() || (id.getDepth() < 64));
}

inline
bool
SHAMapTreeNode::isValid () const
{
    return mType != tnERROR;
}

inline
bool
SHAMapTreeNode::isTransaction () const
{
    return (mType == tnTRANSACTION_NM) || (mType == tnTRANSACTION_MD);
}

inline
bool
SHAMapTreeNode::hasMetaData () const
{
    return mType == tnTRANSACTION_MD;
}

inline
bool
SHAMapTreeNode::isAccountState () const
{
    return mType == tnACCOUNT_STATE;
}

inline
bool
SHAMapTreeNode::isInnerNode () const
{
    return !mItem;
}

inline
bool
SHAMapTreeNode::isEmptyBranch (int m) const
{
    return (mIsBranch & (1 << m)) == 0;
}

inline
uint256 const&
SHAMapTreeNode::getChildHash (int m) const
{
    assert ((m >= 0) && (m < 16) && (mType == tnINNER));
    return mHashes[m];
}

inline
bool
SHAMapTreeNode::hasItem () const
{
    return bool(mItem);
}

inline
std::shared_ptr<SHAMapItem> const&
SHAMapTreeNode::peekItem () const
{
    return mItem;
}

inline
bool
SHAMapTreeNode::isFullBelow (std::uint32_t generation) const
{
    return mFullBelowGen == generation;
}

inline
void
SHAMapTreeNode::setFullBelowGen (std::uint32_t gen)
{
    mFullBelowGen = gen;
}

} // ripple

#endif
