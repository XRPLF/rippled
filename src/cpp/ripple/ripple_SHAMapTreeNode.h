//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_SHAMAPTREENODE_H
#define RIPPLE_SHAMAPTREENODE_H

DEFINE_INSTANCE (SHAMapTreeNode);

class SHAMap;

enum SHANodeFormat
{
    snfPREFIX   = 1, // Form that hashes to its official hash
    snfWIRE     = 2, // Compressed form used on the wire
    snfHASH     = 3, // just the hash
};

class SHAMapTreeNode : public SHAMapNode, public IS_INSTANCE (SHAMapTreeNode)
{
public:
    typedef boost::shared_ptr<SHAMapTreeNode>           pointer;
    typedef const boost::shared_ptr<SHAMapTreeNode>&    ref;

    enum TNType
    {
        tnERROR             = 0,
        tnINNER             = 1,
        tnTRANSACTION_NM    = 2, // transaction, no metadata
        tnTRANSACTION_MD    = 3, // transaction, with metadata
        tnACCOUNT_STATE     = 4
    };

public:
    SHAMapTreeNode (uint32 seq, const SHAMapNode & nodeID); // empty node
    SHAMapTreeNode (const SHAMapTreeNode & node, uint32 seq); // copy node from older tree
    SHAMapTreeNode (const SHAMapNode & nodeID, SHAMapItem::ref item, TNType type, uint32 seq);

    // raw node functions
    SHAMapTreeNode (const SHAMapNode & id, Blob const & data, uint32 seq,
                    SHANodeFormat format, uint256 const & hash, bool hashValid);
    void addRaw (Serializer&, SHANodeFormat format);

    virtual bool isPopulated () const
    {
        return true;
    }

    // node functions
    uint32 getSeq () const
    {
        return mSeq;
    }
    void setSeq (uint32 s)
    {
        mAccessSeq = mSeq = s;
    }
    void touch (uint32 s)
    {
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
    {
        return mItem;
    }
    SHAMapItem::pointer getItem () const;
    bool setItem (SHAMapItem::ref i, TNType type);
    uint256 const& getTag () const
    {
        return mItem->getTag ();
    }
    Blob const& peekData ()
    {
        return mItem->peekData ();
    }
    Blob getData () const
    {
        return mItem->getData ();
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

private:
    // VFALCO TODO derive from Uncopyable
    SHAMapTreeNode (const SHAMapTreeNode&); // no implementation
    SHAMapTreeNode& operator= (const SHAMapTreeNode&); // no implementation

    // VFALCO TODO remove the use of friend
    friend class SHAMap;

    uint256             mHash;
    uint256             mHashes[16];
    SHAMapItem::pointer mItem;
    uint32              mSeq, mAccessSeq;
    TNType              mType;
    int                 mIsBranch;
    bool                mFullBelow;

    bool updateHash ();
};

#endif
