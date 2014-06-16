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

#include <ripple/module/app/main/FullBelowCache.h>
#include <ripple/nodestore/NodeObject.h>
#include <ripple/unity/radmap.h>
#include <boost/thread/mutex.hpp>
#include <boost/thread/shared_lock_guard.hpp>
#include <boost/thread/shared_mutex.hpp>

#include <unordered_map>

namespace std {

template <>
struct hash <ripple::SHAMapNode>
{
    std::size_t operator() (ripple::SHAMapNode const& value) const
    {
        return value.getMHash ();
    }
};

}

//------------------------------------------------------------------------------

namespace boost {

template <>
struct hash <ripple::SHAMapNode> : std::hash <ripple::SHAMapNode>
{
};

}

//------------------------------------------------------------------------------

namespace ripple {

enum SHAMapState
{
    smsModifying = 0,       // Objects can be added and removed (like an open ledger)
    smsImmutable = 1,       // Map cannot be changed (like a closed ledger)
    smsSynching = 2,        // Map's hash is locked in, valid nodes can be added (like a peer's closing ledger)
    smsFloating = 3,        // Map is free to change hash (like a synching open ledger)
    smsInvalid = 4,         // Map is known not to be valid (usually synching a corrupt ledger)
};

/** A SHAMap is both a radix tree with a fan-out of 16 and a Merkle tree.

    A radix tree is a tree with two properties:

      1. The key for a node is represented by the node's position in the tree
         (the "prefix property").
      2. A node with only one child is merged with that child
         (the "merge property")

    These properties in a significantly smaller memory footprint for a radix tree.

    And a fan-out of 16 means that each node in the tree has at most 16 children.
    See https://en.wikipedia.org/wiki/Radix_tree

    A Merkle tree is a tree where each non-leaf node is labelled with the hash
    of the combined labels of its children nodes.

    A key property of a Merkle tree is that testing for node inclusion is
    O(log(N)) where N is the number of nodes in the tree.

    See https://en.wikipedia.org/wiki/Merkle_tree
 */
class SHAMap
//     : public CountedObject <SHAMap>
{
private:
    /** Function object which handles missing nodes. */
    typedef std::function <void (std::uint32_t refNum)> MissingNodeHandler;

    /** Default handler which calls NetworkOPs. */
    struct DefaultMissingNodeHandler
    {
        void operator() (std::uint32_t refNUm);
    };

public:
    enum
    {
        STATE_MAP_BUCKETS = 1024
    };

    static char const* getCountedObjectName () { return "SHAMap"; }

    typedef std::shared_ptr<SHAMap> pointer;
    typedef const std::shared_ptr<SHAMap>& ref;

    typedef std::pair<SHAMapItem::pointer, SHAMapItem::pointer> DeltaItem;
    typedef std::pair<SHAMapItem::ref, SHAMapItem::ref> DeltaRef;
    typedef std::map<uint256, DeltaItem> Delta;
    typedef ripple::unordered_map<SHAMapNode, SHAMapTreeNode::pointer, SHAMapNode_hash> NodeMap;
    typedef std::unordered_set<SHAMapNode, SHAMapNode_hash> DirtySet;

    typedef boost::shared_mutex LockType;
    typedef boost::shared_lock<LockType> ScopedReadLockType;
    typedef boost::unique_lock<LockType> ScopedWriteLockType;

public:
    // build new map
    explicit SHAMap (SHAMapType t, FullBelowCache& fullBelowCache,
        std::uint32_t seq = 1, MissingNodeHandler missing_node_handler = DefaultMissingNodeHandler());

    SHAMap (SHAMapType t, uint256 const& hash, FullBelowCache& fullBelowCache,
        MissingNodeHandler missing_node_handler = DefaultMissingNodeHandler());

    ~SHAMap ();

    std::size_t size () const noexcept
    {
        return mTNByID.size ();
    }

    // Returns a new map that's a snapshot of this one. Force CoW
    SHAMap::pointer snapShot (bool isMutable);

    // Remove nodes from memory
    void dropCache ();

    void setLedgerSeq (std::uint32_t lseq)
    {
        mLedgerSeq = lseq;
    }

    bool hasNode (const SHAMapNode & id);
    bool fetchRoot (uint256 const & hash, SHAMapSyncFilter * filter);

    // normal hash access functions
    bool hasItem (uint256 const & id);
    bool delItem (uint256 const & id);
    bool addItem (const SHAMapItem & i, bool isTransaction, bool hasMeta);
    bool updateItem (const SHAMapItem & i, bool isTransaction, bool hasMeta);
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
    void visitLeaves(std::function<void (SHAMapItem::ref)>);

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
    bool isImmutable ()
    {
        return mState == smsImmutable;
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
    int flushDirty (DirtySet & dirtySet, int maxNodes, NodeObjectType t,
                           std::uint32_t seq);
    std::shared_ptr<DirtySet> disarmDirty ();

    void setSeq (std::uint32_t seq)
    {
        mSeq = seq;
        assert (seq != 0);
    }
    std::uint32_t getSeq ()
    {
        return mSeq;
    }

    // overloads for backed maps
    SHAMapTreeNode::pointer fetchNodeExternal (const SHAMapNode & id, uint256 const & hash); // throws
    SHAMapTreeNode::pointer fetchNodeExternalNT (const SHAMapNode & id, uint256 const & hash); // no throw

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
    void getFetchPack (SHAMap * have, bool includeLeaves, int max, std::function<void (const uint256&, const Blob&)>);

    // VFALCO NOTE These static members should be moved into a
    //             new Application singleton class.
    //
    // tree node cache operations
    static SHAMapTreeNode::pointer getCache (uint256 const& hash, SHAMapNode const& id);
    static void canonicalize (uint256 const& hash, SHAMapTreeNode::pointer&);

    static int getTreeNodeSize ()
    {
        return treeNodeCache.getCacheSize ();
    }

    static void sweep ()
    {
        treeNodeCache.sweep ();
    }

    static void setTreeCache (int size, int age)
    {
        treeNodeCache.setTargetSize (size);
        treeNodeCache.setTargetAge (age);
    }

    void setTXMap ()
    {
        mTXMap = true;
    }

    typedef std::pair<uint256, SHAMapNode> TNIndex;

private:
    static TaggedCache <uint256, SHAMapTreeNode> treeNodeCache;

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

    // Non-blocking version of getNodePointerNT
    SHAMapTreeNode* getNodeAsync (
        const SHAMapNode & id, uint256 const & hash, SHAMapSyncFilter * filter, bool& pending);

    SHAMapItem::pointer onlyBelow (SHAMapTreeNode*);
    void eraseChildren (SHAMapTreeNode::pointer);
    void dropBelow (SHAMapTreeNode*);
    bool hasInnerNode (const SHAMapNode & nodeID, uint256 const & hash);
    bool hasLeafNode (uint256 const & tag, uint256 const & hash);

    bool walkBranch (SHAMapTreeNode * node, SHAMapItem::ref otherMapItem, bool isFirstMap,
                     Delta & differences, int & maxCount);

    void visitLeavesInternal (std::function<void (SHAMapItem::ref item)>& function);

private:

    // This lock protects key SHAMap structures.
    // One may change anything with a write lock.
    // With a read lock, one may not invalidate pointers to existing members of mTNByID
    mutable LockType mLock;

    FullBelowCache& m_fullBelowCache;
    std::uint32_t mSeq;
    std::uint32_t mLedgerSeq; // sequence number of ledger this is part of
    SyncUnorderedMapType< SHAMapNode, SHAMapTreeNode::pointer, SHAMapNode_hash > mTNByID;
    std::shared_ptr<DirtySet> mDirtyNodes;
    SHAMapTreeNode::pointer root;
    SHAMapState mState;
    SHAMapType mType;
    bool mTXMap;       // Map of transactions without metadata
    MissingNodeHandler m_missing_node_handler;
};

}

#endif
