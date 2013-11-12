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

#ifndef RIPPLE_SHAMAP_H
#define RIPPLE_SHAMAP_H

enum SHAMapState
{
    smsModifying = 0,       // Objects can be added and removed (like an open ledger)
    smsImmutable = 1,       // Map cannot be changed (like a closed ledger)
    smsSynching = 2,        // Map's hash is locked in, valid nodes can be added (like a peer's closing ledger)
    smsFloating = 3,        // Map is free to change hash (like a synching open ledger)
    smsInvalid = 4,         // Map is known not to be valid (usually synching a corrupt ledger)
};

class SHAMap
    : public CountedObject <SHAMap>
{
public:
    static char const* getCountedObjectName () { return "SHAMap"; }

    typedef boost::shared_ptr<SHAMap> pointer;
    typedef const boost::shared_ptr<SHAMap>& ref;

    typedef std::pair<SHAMapItem::pointer, SHAMapItem::pointer> DeltaItem;
    typedef std::map<uint256, DeltaItem> Delta;
    typedef boost::unordered_map<SHAMapNode, SHAMapTreeNode::pointer> DirtyMap;

    typedef RippleRecursiveMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;

public:
    // build new map
    explicit SHAMap (SHAMapType t, uint32 seq = 1);
    SHAMap (SHAMapType t, uint256 const & hash);

    ~SHAMap ();

    std::size_t size () const noexcept
    {
        return mTNByID.size ();
    }

    // Returns a new map that's a snapshot of this one. Force CoW
    SHAMap::pointer snapShot (bool isMutable);

    // Remove nodes from memory
    void dropCache ();

    void setLedgerSeq (uint32 lseq)
    {
        mLedgerSeq = lseq;
    }

    // hold the map stable across operations
    LockType const& peekMutex () const
    {
        return mLock;
    }

    bool hasNode (const SHAMapNode & id);
    bool fetchRoot (uint256 const & hash, SHAMapSyncFilter * filter);

    // normal hash access functions
    bool hasItem (uint256 const & id);
    bool delItem (uint256 const & id);
    bool addItem (const SHAMapItem & i, bool isTransaction, bool hasMeta);
    bool updateItem (const SHAMapItem & i, bool isTransaction, bool hasMeta);
    SHAMapItem getItem (uint256 const & id);
    uint256 getHash () const
    {
        return root->getNodeHash ();
    }
    uint256 getHash ()
    {
        return root->getNodeHash ();
    }

    // save a copy if you have a temporary anyway
    bool updateGiveItem (SHAMapItem::ref, bool isTransaction, bool hasMeta);
    bool addGiveItem (SHAMapItem::ref, bool isTransaction, bool hasMeta);

    // save a copy if you only need a temporary
    SHAMapItem::pointer peekItem (uint256 const & id);
    SHAMapItem::pointer peekItem (uint256 const & id, uint256 & hash);
    SHAMapItem::pointer peekItem (uint256 const & id, SHAMapTreeNode::TNType & type);

    // traverse functions
    SHAMapItem::pointer peekFirstItem ();
    SHAMapItem::pointer peekFirstItem (SHAMapTreeNode::TNType & type);
    SHAMapItem::pointer peekLastItem ();
    SHAMapItem::pointer peekNextItem (uint256 const& );
    SHAMapItem::pointer peekNextItem (uint256 const& , SHAMapTreeNode::TNType & type);
    SHAMapItem::pointer peekPrevItem (uint256 const& );
    void visitLeaves(FUNCTION_TYPE<void (SHAMapItem::ref)>);

    // comparison/sync functions
    void getMissingNodes (std::vector<SHAMapNode>& nodeIDs, std::vector<uint256>& hashes, int max,
                          SHAMapSyncFilter * filter);
    bool getNodeFat (const SHAMapNode & node, std::vector<SHAMapNode>& nodeIDs,
                     std::list<Blob >& rawNode, bool fatRoot, bool fatLeaves);
    bool getRootNode (Serializer & s, SHANodeFormat format);
    std::vector<uint256> getNeededHashes (int max, SHAMapSyncFilter * filter);
    SHAMapAddNode addRootNode (uint256 const & hash, Blob const & rootNode, SHANodeFormat format,
                               SHAMapSyncFilter * filter);
    SHAMapAddNode addRootNode (Blob const & rootNode, SHANodeFormat format,
                               SHAMapSyncFilter * filter);
    SHAMapAddNode addKnownNode (const SHAMapNode & nodeID, Blob const & rawNode,
                                SHAMapSyncFilter * filter);

    // status functions
    void setImmutable ()
    {
        assert (mState != smsInvalid);
        mState = smsImmutable;
    }
    void clearImmutable ()
    {
        mState = smsModifying;
    }
    bool isSynching () const
    {
        return (mState == smsFloating) || (mState == smsSynching);
    }
    void setSynching ()
    {
        mState = smsSynching;
    }
    void setFloating ()
    {
        mState = smsFloating;
    }
    void clearSynching ()
    {
        mState = smsModifying;
    }
    bool isValid ()
    {
        return mState != smsInvalid;
    }

    // caution: otherMap must be accessed only by this function
    // return value: true=successfully completed, false=too different
    bool compare (SHAMap::ref otherMap, Delta & differences, int maxCount);

    int armDirty ();
    static int flushDirty (DirtyMap & dirtyMap, int maxNodes, NodeObjectType t, uint32 seq);
    boost::shared_ptr<DirtyMap> disarmDirty ();

    void setSeq (uint32 seq)
    {
        mSeq = seq;
    }
    uint32 getSeq ()
    {
        return mSeq;
    }

    // overloads for backed maps
    boost::shared_ptr<SHAMapTreeNode> fetchNodeExternal (const SHAMapNode & id, uint256 const & hash); // throws
    boost::shared_ptr<SHAMapTreeNode> fetchNodeExternalNT (const SHAMapNode & id, uint256 const & hash); // no throw

    bool operator== (const SHAMap & s)
    {
        return getHash () == s.getHash ();
    }

    // trusted path operations - prove a particular node is in a particular ledger
    std::list<Blob > getTrustedPath (uint256 const & index);
    static Blob checkTrustedPath (uint256 const & ledgerHash, uint256 const & leafIndex,
                                  const std::list<Blob >& path);

    void walkMap (std::vector<SHAMapMissingNode>& missingNodes, int maxMissing);

    bool getPath (uint256 const & index, std::vector< Blob >& nodes, SHANodeFormat format);

    bool deepCompare (SHAMap & other);

    virtual void dump (bool withHashes = false);

    typedef std::pair <uint256, Blob> fetchPackEntry_t;

    std::list<fetchPackEntry_t> getFetchPack (SHAMap * have, bool includeLeaves, int max);
    void getFetchPack (SHAMap * have, bool includeLeaves, int max, FUNCTION_TYPE<void (const uint256&, const Blob&)>);

    static int getFullBelowSize ()
    {
        return fullBelowCache.getSize ();
    }
    static void sweep ()
    {
        fullBelowCache.sweep ();
    }

private:
    static KeyCache <uint256, UptimeTimerAdapter> fullBelowCache;

    void dirtyUp (std::stack<SHAMapTreeNode::pointer>& stack, uint256 const & target, uint256 prevHash);
    std::stack<SHAMapTreeNode::pointer> getStack (uint256 const & id, bool include_nonmatching_leaf);
    SHAMapTreeNode::pointer walkTo (uint256 const & id, bool modify);
    SHAMapTreeNode* walkToPointer (uint256 const & id);
    SHAMapTreeNode::pointer checkCacheNode (const SHAMapNode&);
    void returnNode (SHAMapTreeNode::pointer&, bool modify);
    void trackNewNode (SHAMapTreeNode::pointer&);

    SHAMapTreeNode::pointer getNode (const SHAMapNode & id);
    SHAMapTreeNode::pointer getNode (const SHAMapNode & id, uint256 const & hash, bool modify);
    SHAMapTreeNode* getNodePointer (const SHAMapNode & id);
    SHAMapTreeNode* getNodePointer (const SHAMapNode & id, uint256 const & hash);
    SHAMapTreeNode* getNodePointerNT (const SHAMapNode & id, uint256 const & hash);
    SHAMapTreeNode* getNodePointer (const SHAMapNode & id, uint256 const & hash, SHAMapSyncFilter * filter);
    SHAMapTreeNode* getNodePointerNT (const SHAMapNode & id, uint256 const & hash, SHAMapSyncFilter * filter);
    SHAMapTreeNode* firstBelow (SHAMapTreeNode*);
    SHAMapTreeNode* lastBelow (SHAMapTreeNode*);

    SHAMapItem::pointer onlyBelow (SHAMapTreeNode*);
    void eraseChildren (SHAMapTreeNode::pointer);
    void dropBelow (SHAMapTreeNode*);
    bool hasInnerNode (const SHAMapNode & nodeID, uint256 const & hash);
    bool hasLeafNode (uint256 const & tag, uint256 const & hash);

    bool walkBranch (SHAMapTreeNode * node, SHAMapItem::ref otherMapItem, bool isFirstMap,
                     Delta & differences, int & maxCount);

private:
#if 1
    LockType mLock;
#else
    mutable LockType mLock;
#endif

    uint32 mSeq;
    uint32 mLedgerSeq; // sequence number of ledger this is part of
    boost::unordered_map<SHAMapNode, SHAMapTreeNode::pointer> mTNByID;

    boost::shared_ptr<DirtyMap> mDirtyNodes;

    SHAMapTreeNode::pointer root;

    SHAMapState mState;

    SHAMapType mType;
};

#endif
// vim:ts=4
