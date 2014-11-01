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

#include <ripple/app/shamap/SHAMapAddNode.h>
#include <ripple/app/shamap/SHAMapItem.h>
#include <ripple/app/shamap/SHAMapMissingNode.h>
#include <ripple/app/shamap/SHAMapNodeID.h>
#include <ripple/app/shamap/SHAMapSyncFilter.h>
#include <ripple/app/shamap/SHAMapTreeNode.h>
#include <ripple/basics/LoggedTimings.h>
#include <ripple/common/UnorderedContainers.h>
#include <ripple/app/main/FullBelowCache.h>
#include <ripple/nodestore/NodeObject.h>
#include <boost/thread/mutex.hpp>
#include <boost/thread/shared_lock_guard.hpp>
#include <boost/thread/shared_mutex.hpp>

namespace std {

template <>
struct hash <ripple::SHAMapNodeID>
{
    std::size_t operator() (ripple::SHAMapNodeID const& value) const
    {
        return value.getMHash ();
    }
};

}

//------------------------------------------------------------------------------

namespace boost {

template <>
struct hash <ripple::SHAMapNodeID> : std::hash <ripple::SHAMapNodeID>
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
    typedef hash_map<SHAMapNodeID, SHAMapTreeNode::pointer, SHAMapNode_hash> NodeMap;

    typedef std::stack<std::pair<SHAMapTreeNode::pointer, SHAMapNodeID>> SharedPtrNodeStack;

public:
    // build new map
    SHAMap (
        SHAMapType t,
        FullBelowCache& fullBelowCache,
        TreeNodeCache& treeNodeCache,
        std::uint32_t seq = 1,
        MissingNodeHandler missing_node_handler = DefaultMissingNodeHandler());

    SHAMap (
        SHAMapType t,
        uint256 const& hash,
        FullBelowCache& fullBelowCache,
        TreeNodeCache& treeNodeCache,
        MissingNodeHandler missing_node_handler = DefaultMissingNodeHandler());

    ~SHAMap ();

    // Returns a new map that's a snapshot of this one.
    // Handles copy on write for mutable snapshots.
    SHAMap::pointer snapShot (bool isMutable);

    void setLedgerSeq (std::uint32_t lseq)
    {
        mLedgerSeq = lseq;
    }

    bool fetchRoot (uint256 const& hash, SHAMapSyncFilter * filter);

    // normal hash access functions
    bool hasItem (uint256 const& id);
    bool delItem (uint256 const& id);
    bool addItem (SHAMapItem const& i, bool isTransaction, bool hasMeta);

    uint256 getHash () const
    {
        return root->getNodeHash ();
    }

    // save a copy if you have a temporary anyway
    bool updateGiveItem (SHAMapItem::ref, bool isTransaction, bool hasMeta);
    bool addGiveItem (SHAMapItem::ref, bool isTransaction, bool hasMeta);

    // save a copy if you only need a temporary
    SHAMapItem::pointer peekItem (uint256 const& id);
    SHAMapItem::pointer peekItem (uint256 const& id, uint256 & hash);
    SHAMapItem::pointer peekItem (uint256 const& id, SHAMapTreeNode::TNType & type);

    // traverse functions
    SHAMapItem::pointer peekFirstItem ();
    SHAMapItem::pointer peekFirstItem (SHAMapTreeNode::TNType & type);
    SHAMapItem::pointer peekLastItem ();
    SHAMapItem::pointer peekNextItem (uint256 const& );
    SHAMapItem::pointer peekNextItem (uint256 const& , SHAMapTreeNode::TNType & type);
    SHAMapItem::pointer peekPrevItem (uint256 const& );

    void visitNodes (std::function<void (SHAMapTreeNode&)> const&);
    void visitLeaves(std::function<void (SHAMapItem::ref)> const&);

    // comparison/sync functions
    void getMissingNodes (std::vector<SHAMapNodeID>& nodeIDs, std::vector<uint256>& hashes, int max,
                          SHAMapSyncFilter * filter);
    bool getNodeFat (SHAMapNodeID node, std::vector<SHAMapNodeID>& nodeIDs,
                     std::list<Blob >& rawNode, bool fatRoot, bool fatLeaves);
    bool getRootNode (Serializer & s, SHANodeFormat format);
    std::vector<uint256> getNeededHashes (int max, SHAMapSyncFilter * filter);
    SHAMapAddNode addRootNode (uint256 const& hash, Blob const& rootNode, SHANodeFormat format,
                               SHAMapSyncFilter * filter);
    SHAMapAddNode addRootNode (Blob const& rootNode, SHANodeFormat format,
                               SHAMapSyncFilter * filter);
    SHAMapAddNode addKnownNode (SHAMapNodeID const& nodeID, Blob const& rawNode,
                                SHAMapSyncFilter * filter);

    // status functions
    void setImmutable ()
    {
        assert (mState != smsInvalid);
        mState = smsImmutable;
    }
    bool isSynching () const
    {
        return (mState == smsFloating) || (mState == smsSynching);
    }
    void setSynching ()
    {
        mState = smsSynching;
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

    int flushDirty (NodeObjectType t, std::uint32_t seq);
    int unshare ();

    void walkMap (std::vector<SHAMapMissingNode>& missingNodes, int maxMissing);

    bool deepCompare (SHAMap & other);

    typedef std::pair <uint256, Blob> fetchPackEntry_t;

    void getFetchPack (SHAMap * have, bool includeLeaves, int max, std::function<void (uint256 const&, const Blob&)>);

    void setUnbacked ()
    {
        mBacked = false;
    }

    void dump (bool withHashes = false);

private:
    // trusted path operations - prove a particular node is in a particular ledger
    std::list<Blob > getTrustedPath (uint256 const& index);

    bool getPath (uint256 const& index, std::vector< Blob >& nodes, SHANodeFormat format);

     // tree node cache operations
    SHAMapTreeNode::pointer getCache (uint256 const& hash);
    void canonicalize (uint256 const& hash, SHAMapTreeNode::pointer&);

    // database operations
    SHAMapTreeNode::pointer fetchNodeFromDB (uint256 const& hash);

    SHAMapTreeNode::pointer fetchNodeNT (uint256 const& hash);

    SHAMapTreeNode::pointer fetchNodeNT (
        SHAMapNodeID const& id,
        uint256 const& hash,
        SHAMapSyncFilter *filter);

    SHAMapTreeNode::pointer fetchNode (uint256 const& hash);

    SHAMapTreeNode::pointer checkFilter (uint256 const& hash, SHAMapNodeID const& id,
        SHAMapSyncFilter* filter);

    /** Update hashes up to the root */
    void dirtyUp (SharedPtrNodeStack& stack,
                  uint256 const& target, SHAMapTreeNode::pointer terminal);

    /** Get the path from the root to the specified node */
    SharedPtrNodeStack
        getStack (uint256 const& id, bool include_nonmatching_leaf);

    /** Walk to the specified index, returning the node */
    SHAMapTreeNode* walkToPointer (uint256 const& id);

    /** Unshare the node, allowing it to be modified */
    void unshareNode (SHAMapTreeNode::pointer&, SHAMapNodeID const& nodeID);

    /** prepare a node to be modified before flushing */
    void preFlushNode (SHAMapTreeNode::pointer& node);

    /** write and canonicalize modified node */
    void writeNode (NodeObjectType t, std::uint32_t seq,
        SHAMapTreeNode::pointer& node);

    SHAMapTreeNode* firstBelow (SHAMapTreeNode*);
    SHAMapTreeNode* lastBelow (SHAMapTreeNode*);

    // Simple descent
    // Get a child of the specified node
    SHAMapTreeNode* descend (SHAMapTreeNode*, int branch);
    SHAMapTreeNode* descendThrow (SHAMapTreeNode*, int branch);
    SHAMapTreeNode::pointer descend (SHAMapTreeNode::ref, int branch);
    SHAMapTreeNode::pointer descendThrow (SHAMapTreeNode::ref, int branch);

    // Descend with filter
    SHAMapTreeNode* descendAsync (SHAMapTreeNode* parent, int branch,
        SHAMapNodeID const& childID, SHAMapSyncFilter* filter, bool& pending);

    std::pair <SHAMapTreeNode*, SHAMapNodeID>
        descend (SHAMapTreeNode* parent, SHAMapNodeID const& parentID,
        int branch, SHAMapSyncFilter* filter);

    // Non-storing
    // Does not hook the returned node to its parent
    SHAMapTreeNode::pointer descendNoStore (SHAMapTreeNode::ref, int branch);

    /** If there is only one leaf below this node, get its contents */
    SHAMapItem::pointer onlyBelow (SHAMapTreeNode*);

    bool hasInnerNode (SHAMapNodeID const& nodeID, uint256 const& hash);
    bool hasLeafNode (uint256 const& tag, uint256 const& hash);

    bool walkBranch (SHAMapTreeNode* node,
                     SHAMapItem::ref otherMapItem, bool isFirstMap,
                     Delta & differences, int & maxCount);

    void visitLeavesInternal (std::function<void (SHAMapItem::ref item)>& function);

    int walkSubTree (bool doWrite, NodeObjectType t, std::uint32_t seq);

private:

    FullBelowCache& m_fullBelowCache;
    std::uint32_t mSeq;
    std::uint32_t mLedgerSeq; // sequence number of ledger this is part of
    TreeNodeCache& mTreeNodeCache;
    SHAMapTreeNode::pointer root;
    SHAMapState mState;
    SHAMapType mType;
    bool mBacked;       // Map is backed by the database
    MissingNodeHandler m_missing_node_handler;
};

}

#endif
