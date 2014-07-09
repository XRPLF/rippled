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

#ifndef RIPPLE_SHAMAPTREENODE_H
#define RIPPLE_SHAMAPTREENODE_H

#include <ripple/module/app/shamap/SHAMapNodeID.h>

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

    SHAMapTreeNode (std::uint32_t seq, const SHAMapNodeID & nodeID); // empty node
    SHAMapTreeNode (const SHAMapTreeNode & node, std::uint32_t seq); // copy node from older tree
    SHAMapTreeNode (const SHAMapNodeID & nodeID, SHAMapItem::ref item, TNType type,
                    std::uint32_t seq);

    // raw node functions
    SHAMapTreeNode (const SHAMapNodeID & id, Blob const & data, std::uint32_t seq,
                    SHANodeFormat format, uint256 const & hash, bool hashValid);
    void addRaw (Serializer&, SHANodeFormat format);

    virtual bool isPopulated () const
    {
        return true;
    }

    // node functions
    std::uint32_t getSeq () const
    {
        return mSeq;
    }
    void setSeq (std::uint32_t s)
    {
        mAccessSeq = mSeq = s;
    }
    void touch (std::uint32_t s)
    {
        if (mSeq != 0)
            mAccessSeq = s;
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
    bool isInBounds () const
    {
        // Nodes at depth 64 must be leaves
        return (!isInner() || (mID.getDepth() < 64));
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
    bool setChildHash (int m, uint256 const & hash);
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
        return !!mItem;
    }
    SHAMapItem::ref peekItem ()
    { // CAUTION: Do not modify the item
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
    bool isFullBelow (void) const
    {
        return mFullBelow;
    }
    void setFullBelow (void)
    {
        mFullBelow = true;
    }

    virtual void dump ();
    virtual std::string getString () const;

    SHAMapNodeID const& getID() const {return mID;}
    void setID(SHAMapNodeID const& id) {mID = id;}

    /** Descends along the specified branch
    * On invocation, nodeID must be the ID of this node
    * Returns false if there is no node down that branch
    * Otherwise, returns true and fills in the node's ID and hash
    *
    * @param branch   the branch to descend [0, 15]
    * @param nodeID   on entry the ID of the parent. On exit the ID of the child
    * @param nodeHash on exit the hash of the child node.
    * @return true if nodeID and nodeHash are altered.
    */
    bool descend (int branch, SHAMapNodeID& nodeID, uint256& nodeHash);

private:

    // VFALCO TODO remove the use of friend
    friend class SHAMap;

    SHAMapNodeID        mID;
    uint256             mHash;
    uint256             mHashes[16];
    SHAMapItem::pointer mItem;
    std::uint32_t       mSeq, mAccessSeq;
    TNType              mType;
    int                 mIsBranch;
    bool                mFullBelow;

    bool updateHash ();
};

using TreeNodeCache = TaggedCache <uint256, SHAMapTreeNode>;

} // ripple

#endif
