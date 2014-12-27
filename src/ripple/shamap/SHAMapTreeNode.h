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

#ifndef RIPPLE_SHAMAP_SHAMAPTREENODE_H
#define RIPPLE_SHAMAP_SHAMAPTREENODE_H

#include <ripple/shamap/SHAMapItem.h>
#include <ripple/shamap/SHAMapNodeID.h>
#include <ripple/shamap/TreeNodeCache.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/basics/TaggedCache.h>
#include <beast/utility/Journal.h>

namespace ripple {

class SHAMap;

enum SHANodeFormat
{
    snfPREFIX   = 1, // Form that hashes to its official hash
    snfWIRE     = 2, // Compressed form used on the wire
    snfHASH     = 3, // just the hash
};

class SHAMapTreeNode
    : public CountedObject <SHAMapTreeNode>
{
public:
    static char const* getCountedObjectName () { return "SHAMapTreeNode"; }

    typedef std::shared_ptr<SHAMapTreeNode>           pointer;
    typedef const std::shared_ptr<SHAMapTreeNode>&    ref;

    enum TNType
    {
        tnERROR             = 0,
        tnINNER             = 1,
        tnTRANSACTION_NM    = 2, // transaction, no metadata
        tnTRANSACTION_MD    = 3, // transaction, with metadata
        tnACCOUNT_STATE     = 4
    };

public:
    SHAMapTreeNode (const SHAMapTreeNode&) = delete;
    SHAMapTreeNode& operator= (const SHAMapTreeNode&) = delete;

    SHAMapTreeNode (std::uint32_t seq); // empty node
    SHAMapTreeNode (const SHAMapTreeNode & node, std::uint32_t seq); // copy node from older tree
    SHAMapTreeNode (SHAMapItem::ref item, TNType type, std::uint32_t seq);

    // raw node functions
    SHAMapTreeNode (Blob const & data, std::uint32_t seq,
                    SHANodeFormat format, uint256 const& hash, bool hashValid);
    void addRaw (Serializer&, SHANodeFormat format);

    // node functions
    std::uint32_t getSeq () const
    {
        return mSeq;
    }
    void setSeq (std::uint32_t s)
    {
        mSeq = s;
    }
    uint256 const& getNodeHash () const
    {
        return mHash;
    }
    TNType getType () const
    {
        return mType;
    }

    // type functions
    bool isLeaf () const
    {
        return (mType == tnTRANSACTION_NM) || (mType == tnTRANSACTION_MD) ||
               (mType == tnACCOUNT_STATE);
    }
    bool isInner () const
    {
        return mType == tnINNER;
    }
    bool isInBounds (SHAMapNodeID const &id) const
    {
        // Nodes at depth 64 must be leaves
        return (!isInner() || (id.getDepth() < 64));
    }
    bool isValid () const
    {
        return mType != tnERROR;
    }
    bool isTransaction () const
    {
        return (mType == tnTRANSACTION_NM) || (mType == tnTRANSACTION_MD);
    }
    bool hasMetaData () const
    {
        return mType == tnTRANSACTION_MD;
    }
    bool isAccountState () const
    {
        return mType == tnACCOUNT_STATE;
    }

    // inner node functions
    bool isInnerNode () const
    {
        return !mItem;
    }

    // We are modifying the child hash
    bool setChild (int m, uint256 const& hash, std::shared_ptr<SHAMapTreeNode> const& child);

    // We are sharing/unsharing the child
    void shareChild (int m, std::shared_ptr<SHAMapTreeNode> const& child);

    bool isEmptyBranch (int m) const
    {
        return (mIsBranch & (1 << m)) == 0;
    }
    bool isEmpty () const;
    int getBranchCount () const;
    void makeInner ();
    uint256 const& getChildHash (int m) const
    {
        assert ((m >= 0) && (m < 16) && (mType == tnINNER));
        return mHashes[m];
    }

    // item node function
    bool hasItem () const
    {
        return bool(mItem);
    }
    SHAMapItem::ref peekItem ()
    {
        // CAUTION: Do not modify the item TODO(tom): a comment in the code does
        // nothing - this should return a const reference.
        return mItem;
    }
    bool setItem (SHAMapItem::ref i, TNType type);
    uint256 const& getTag () const
    {
        return mItem->getTag ();
    }
    Blob const& peekData ()
    {
        return mItem->peekData ();
    }

    // sync functions
    bool isFullBelow (std::uint32_t generation) const
    {
        return mFullBelowGen == generation;
    }
    void setFullBelowGen (std::uint32_t gen)
    {
        mFullBelowGen = gen;
    }

    // VFALCO Why is this virtual?
    virtual void dump (SHAMapNodeID const&, beast::Journal journal);
    virtual std::string getString (SHAMapNodeID const&) const;

    SHAMapTreeNode* getChildPointer (int branch);
    SHAMapTreeNode::pointer getChild (int branch);
    void canonicalizeChild (int branch, SHAMapTreeNode::pointer& node);

private:

    // VFALCO TODO remove the use of friend
    friend class SHAMap;

    uint256                 mHash;
    uint256                 mHashes[16];
    SHAMapTreeNode::pointer mChildren[16];
    SHAMapItem::pointer     mItem;
    std::uint32_t           mSeq;
    TNType                  mType;
    int                     mIsBranch;
    std::uint32_t           mFullBelowGen;

    bool updateHash ();

    static std::mutex       childLock;
};

} // ripple

#endif
