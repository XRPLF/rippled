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

#ifndef RIPPLE_SHAMAP_SHAMAP_H_INCLUDED
#define RIPPLE_SHAMAP_SHAMAP_H_INCLUDED

#include <ripple/shamap/Family.h>
#include <ripple/shamap/FullBelowCache.h>
#include <ripple/shamap/SHAMapAddNode.h>
#include <ripple/shamap/SHAMapItem.h>
#include <ripple/shamap/SHAMapMissingNode.h>
#include <ripple/shamap/SHAMapNodeID.h>
#include <ripple/shamap/SHAMapSyncFilter.h>
#include <ripple/shamap/SHAMapTreeNode.h>
#include <ripple/shamap/TreeNodeCache.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/nodestore/Database.h>
#include <ripple/nodestore/NodeObject.h>
#include <beast/utility/Journal.h>
#include <boost/thread/mutex.hpp>
#include <boost/thread/shared_lock_guard.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <stack>

namespace ripple {

enum class SHAMapState
{
    Modifying = 0,       // Objects can be added and removed (like an open ledger)
    Immutable = 1,       // Map cannot be changed (like a closed ledger)
    Synching  = 2,       // Map's hash is locked in, valid nodes can be added (like a peer's closing ledger)
    Floating  = 3,       // Map is free to change hash (like a synching open ledger)
    Invalid   = 4,       // Map is known not to be valid (usually synching a corrupt ledger)
};

/** Function object which handles missing nodes. */
using MissingNodeHandler = std::function <void (std::uint32_t refNum)>;

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
    using Family = shamap::Family;

    Family&                         f_;
    beast::Journal                  journal_;
    std::uint32_t                   seq_;
    std::uint32_t                   ledgerSeq_; // sequence number of ledger this is part of
    std::shared_ptr<SHAMapTreeNode> root_;
    SHAMapState                     state_;
    SHAMapType                      type_;
    bool                            backed_ = true; // Map is backed by the database

public:
    using DeltaItem = std::pair<std::shared_ptr<SHAMapItem>,
                                std::shared_ptr<SHAMapItem>>;
    using Delta     = std::map<uint256, DeltaItem>;

    ~SHAMap ();
    SHAMap(SHAMap const&) = delete;
    SHAMap& operator=(SHAMap const&) = delete;

    // build new map
    SHAMap (
        SHAMapType t,
        Family& f,
        beast::Journal journal,
        std::uint32_t seq = 1
        );

    SHAMap (
        SHAMapType t,
        uint256 const& hash,
        Family& f,
        beast::Journal journal);

    // Returns a new map that's a snapshot of this one.
    // Handles copy on write for mutable snapshots.
    std::shared_ptr<SHAMap> snapShot (bool isMutable) const;
    void setLedgerSeq (std::uint32_t lseq);
    bool fetchRoot (uint256 const& hash, SHAMapSyncFilter * filter);

    // normal hash access functions
    bool hasItem (uint256 const& id) const;
    bool delItem (uint256 const& id);
    bool addItem (SHAMapItem const& i, bool isTransaction, bool hasMeta);
    uint256 getHash () const;

    // save a copy if you have a temporary anyway
    bool updateGiveItem (std::shared_ptr<SHAMapItem> const&, bool isTransaction, bool hasMeta);
    bool addGiveItem (std::shared_ptr<SHAMapItem> const&, bool isTransaction, bool hasMeta);

    // save a copy if you only need a temporary
    std::shared_ptr<SHAMapItem> peekItem (uint256 const& id) const;
    std::shared_ptr<SHAMapItem> peekItem (uint256 const& id, uint256 & hash) const;
    std::shared_ptr<SHAMapItem> peekItem (uint256 const& id, SHAMapTreeNode::TNType & type) const;

    // traverse functions
    std::shared_ptr<SHAMapItem> peekFirstItem () const;
    std::shared_ptr<SHAMapItem> peekFirstItem (SHAMapTreeNode::TNType & type) const;
    std::shared_ptr<SHAMapItem> peekLastItem () const;
    std::shared_ptr<SHAMapItem> peekNextItem (uint256 const& ) const;
    std::shared_ptr<SHAMapItem> peekNextItem (uint256 const& , SHAMapTreeNode::TNType & type) const;
    std::shared_ptr<SHAMapItem> peekPrevItem (uint256 const& ) const;

    void visitNodes (std::function<bool (SHAMapTreeNode&)> const&) const;
    void visitLeaves(std::function<void (std::shared_ptr<SHAMapItem> const&)> const&) const;

    // comparison/sync functions
    void getMissingNodes (std::vector<SHAMapNodeID>& nodeIDs, std::vector<uint256>& hashes, int max,
                          SHAMapSyncFilter * filter);
    
    bool getNodeFat (SHAMapNodeID node,
        std::vector<SHAMapNodeID>& nodeIDs,
            std::vector<Blob>& rawNode,
                bool fatLeaves, std::uint32_t depth) const;
    
    bool getRootNode (Serializer & s, SHANodeFormat format) const;
    std::vector<uint256> getNeededHashes (int max, SHAMapSyncFilter * filter);
    SHAMapAddNode addRootNode (uint256 const& hash, Blob const& rootNode, SHANodeFormat format,
                               SHAMapSyncFilter * filter);
    SHAMapAddNode addRootNode (Blob const& rootNode, SHANodeFormat format,
                               SHAMapSyncFilter * filter);
    SHAMapAddNode addKnownNode (SHAMapNodeID const& nodeID, Blob const& rawNode,
                                SHAMapSyncFilter * filter);

    // status functions
    void setImmutable ();
    bool isSynching () const;
    void setSynching ();
    void clearSynching ();
    bool isValid () const;

    // caution: otherMap must be accessed only by this function
    // return value: true=successfully completed, false=too different
    bool compare (std::shared_ptr<SHAMap> const& otherMap,
                  Delta& differences, int maxCount) const;

    int flushDirty (NodeObjectType t, std::uint32_t seq);
    void walkMap (std::vector<SHAMapMissingNode>& missingNodes, int maxMissing) const;
    bool deepCompare (SHAMap & other) const;

    typedef std::pair <uint256, Blob> fetchPackEntry_t;

    void visitDifferences (SHAMap * have, std::function<bool (SHAMapTreeNode&)>) const;

    void getFetchPack (SHAMap * have, bool includeLeaves, int max,
        std::function<void (uint256 const&, const Blob&)>) const;

    void setUnbacked ();

    void dump (bool withHashes = false) const;

private:
    using SharedPtrNodeStack =
        std::stack<std::pair<std::shared_ptr<SHAMapTreeNode>, SHAMapNodeID>>;
    using DeltaRef = std::pair<std::shared_ptr<SHAMapItem> const&,
                               std::shared_ptr<SHAMapItem> const&> ;

    int unshare ();

     // tree node cache operations
    std::shared_ptr<SHAMapTreeNode> getCache (uint256 const& hash) const;
    void canonicalize (uint256 const& hash, std::shared_ptr<SHAMapTreeNode>&) const;

    // database operations
    std::shared_ptr<SHAMapTreeNode> fetchNodeFromDB (uint256 const& hash) const;
    std::shared_ptr<SHAMapTreeNode> fetchNodeNT (uint256 const& hash) const;
    std::shared_ptr<SHAMapTreeNode> fetchNodeNT (
        SHAMapNodeID const& id,
        uint256 const& hash,
        SHAMapSyncFilter *filter) const;
    std::shared_ptr<SHAMapTreeNode> fetchNode (uint256 const& hash) const;
    std::shared_ptr<SHAMapTreeNode> checkFilter (uint256 const& hash, SHAMapNodeID const& id,
        SHAMapSyncFilter* filter) const;

    /** Update hashes up to the root */
    void dirtyUp (SharedPtrNodeStack& stack,
                  uint256 const& target, std::shared_ptr<SHAMapTreeNode> terminal);

    /** Get the path from the root to the specified node */
    SharedPtrNodeStack
        getStack (uint256 const& id, bool include_nonmatching_leaf) const;

    /** Walk to the specified index, returning the node */
    SHAMapTreeNode* walkToPointer (uint256 const& id) const;

    /** Unshare the node, allowing it to be modified */
    void unshareNode (std::shared_ptr<SHAMapTreeNode>&, SHAMapNodeID const& nodeID);

    /** prepare a node to be modified before flushing */
    void preFlushNode (std::shared_ptr<SHAMapTreeNode>& node) const;

    /** write and canonicalize modified node */
    void writeNode (NodeObjectType t, std::uint32_t seq,
        std::shared_ptr<SHAMapTreeNode>& node) const;

    SHAMapTreeNode* firstBelow (SHAMapTreeNode*) const;
    SHAMapTreeNode* lastBelow (SHAMapTreeNode*) const;

    // Simple descent
    // Get a child of the specified node
    SHAMapTreeNode* descend (SHAMapTreeNode*, int branch) const;
    SHAMapTreeNode* descendThrow (SHAMapTreeNode*, int branch) const;
    std::shared_ptr<SHAMapTreeNode> descend (std::shared_ptr<SHAMapTreeNode> const&, int branch) const;
    std::shared_ptr<SHAMapTreeNode> descendThrow (std::shared_ptr<SHAMapTreeNode> const&, int branch) const;

    // Descend with filter
    SHAMapTreeNode* descendAsync (SHAMapTreeNode* parent, int branch,
        SHAMapNodeID const& childID, SHAMapSyncFilter* filter, bool& pending) const;

    std::pair <SHAMapTreeNode*, SHAMapNodeID>
        descend (SHAMapTreeNode* parent, SHAMapNodeID const& parentID,
        int branch, SHAMapSyncFilter* filter) const;

    // Non-storing
    // Does not hook the returned node to its parent
    std::shared_ptr<SHAMapTreeNode> descendNoStore (std::shared_ptr<SHAMapTreeNode> const&, int branch) const;

    /** If there is only one leaf below this node, get its contents */
    std::shared_ptr<SHAMapItem> onlyBelow (SHAMapTreeNode*) const;

    bool hasInnerNode (SHAMapNodeID const& nodeID, uint256 const& hash) const;
    bool hasLeafNode (uint256 const& tag, uint256 const& hash) const;

    bool walkBranch (SHAMapTreeNode* node,
                     std::shared_ptr<SHAMapItem> const& otherMapItem, bool isFirstMap,
                     Delta & differences, int & maxCount) const;
    int walkSubTree (bool doWrite, NodeObjectType t, std::uint32_t seq);
};

inline
void
SHAMap::setLedgerSeq (std::uint32_t lseq)
{
    ledgerSeq_ = lseq;
}

inline
void
SHAMap::setImmutable ()
{
    assert (state_ != SHAMapState::Invalid);
    unshare ();
    state_ = SHAMapState::Immutable;
}

inline
bool
SHAMap::isSynching () const
{
    return (state_ == SHAMapState::Floating) || (state_ == SHAMapState::Synching);
}

inline
void
SHAMap::setSynching ()
{
    state_ = SHAMapState::Synching;
}

inline
void
SHAMap::clearSynching ()
{
    state_ = SHAMapState::Modifying;
}

inline
bool
SHAMap::isValid () const
{
    return state_ != SHAMapState::Invalid;
}

inline
void
SHAMap::setUnbacked ()
{
    backed_ = false;
}

}

#endif
