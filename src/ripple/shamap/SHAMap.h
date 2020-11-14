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

#include <ripple/basics/UnorderedContainers.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/nodestore/Database.h>
#include <ripple/nodestore/NodeObject.h>
#include <ripple/shamap/Family.h>
#include <ripple/shamap/FullBelowCache.h>
#include <ripple/shamap/SHAMapAddNode.h>
#include <ripple/shamap/SHAMapInnerNode.h>
#include <ripple/shamap/SHAMapItem.h>
#include <ripple/shamap/SHAMapLeafNode.h>
#include <ripple/shamap/SHAMapMissingNode.h>
#include <ripple/shamap/SHAMapTreeNode.h>
#include <ripple/shamap/TreeNodeCache.h>
#include <boost/thread/mutex.hpp>
#include <boost/thread/shared_lock_guard.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <cassert>
#include <stack>
#include <vector>

namespace ripple {

class SHAMapNodeID;
class SHAMapSyncFilter;

/** Describes the current state of a given SHAMap */
enum class SHAMapState {
    /** The map is in flux and objects can be added and removed.

        Example: map underlying the open ledger.
     */
    Modifying = 0,

    /** The map is set in stone and cannot be changed.

        Example: a map underlying a given closed ledger.
     */
    Immutable = 1,

    /** The map's hash is fixed but valid nodes may be missing and can be added.

        Example: a map that's syncing a given peer's closing ledger.
     */
    Synching = 2,

    /** The map is known to not be valid.

        Example: usually synching a corrupt ledger.
     */
    Invalid = 3,
};

/** A SHAMap is both a radix tree with a fan-out of 16 and a Merkle tree.

    A radix tree is a tree with two properties:

      1. The key for a node is represented by the node's position in the tree
         (the "prefix property").
      2. A node with only one child is merged with that child
         (the "merge property")

    These properties result in a significantly smaller memory footprint for
    a radix tree.

    A fan-out of 16 means that each node in the tree has at most 16
    children. See https://en.wikipedia.org/wiki/Radix_tree

    A Merkle tree is a tree where each non-leaf node is labelled with the hash
    of the combined labels of its children nodes.

    A key property of a Merkle tree is that testing for node inclusion is
    O(log(N)) where N is the number of nodes in the tree.

    See https://en.wikipedia.org/wiki/Merkle_tree
 */
class SHAMap
{
private:
    Family& f_;
    beast::Journal journal_;

    /** ID to distinguish this map for all others we're sharing nodes with. */
    std::uint32_t cowid_ = 1;

    /** The sequence of the ledger that this map references, if any. */
    std::uint32_t ledgerSeq_ = 0;

    std::shared_ptr<SHAMapTreeNode> root_;
    mutable SHAMapState state_;
    SHAMapType const type_;
    bool backed_ = true;         // Map is backed by the database
    mutable bool full_ = false;  // Map is believed complete in database

public:
    /** Each non-leaf node has 16 children (the 'radix tree' part of the map) */
    static inline constexpr unsigned int branchFactor = 16;

    /** The depth of the hash map: data is only present in the leaves */
    static inline constexpr unsigned int leafDepth = 64;

    using DeltaItem = std::pair<
        std::shared_ptr<SHAMapItem const>,
        std::shared_ptr<SHAMapItem const>>;
    using Delta = std::map<uint256, DeltaItem>;

    SHAMap(SHAMap const&) = delete;
    SHAMap&
    operator=(SHAMap const&) = delete;

    // build new map
    SHAMap(SHAMapType t, Family& f);

    SHAMap(SHAMapType t, uint256 const& hash, Family& f);

    ~SHAMap() = default;

    Family const&
    family() const
    {
        return f_;
    }

    Family&
    family()
    {
        return f_;
    }

    //--------------------------------------------------------------------------

    /** Iterator to a SHAMap's leaves
        This is always a const iterator.
        Meets the requirements of ForwardRange.
    */
    class const_iterator;

    const_iterator
    begin() const;
    const_iterator
    end() const;

    //--------------------------------------------------------------------------

    // Returns a new map that's a snapshot of this one.
    // Handles copy on write for mutable snapshots.
    std::shared_ptr<SHAMap>
    snapShot(bool isMutable) const;

    /*  Mark this SHAMap as "should be full", indicating
        that the local server wants all the corresponding nodes
        in durable storage.
    */
    void
    setFull();

    void
    setLedgerSeq(std::uint32_t lseq);

    bool
    fetchRoot(SHAMapHash const& hash, SHAMapSyncFilter* filter);

    // normal hash access functions

    /** Does the tree have an item with the given ID? */
    bool
    hasItem(uint256 const& id) const;

    bool
    delItem(uint256 const& id);

    bool
    addItem(SHAMapNodeType type, SHAMapItem&& i);

    SHAMapHash
    getHash() const;

    // save a copy if you have a temporary anyway
    bool
    updateGiveItem(SHAMapNodeType type, std::shared_ptr<SHAMapItem const>);

    bool
    addGiveItem(SHAMapNodeType type, std::shared_ptr<SHAMapItem const> item);

    // Save a copy if you need to extend the life
    // of the SHAMapItem beyond this SHAMap
    std::shared_ptr<SHAMapItem const> const&
    peekItem(uint256 const& id) const;
    std::shared_ptr<SHAMapItem const> const&
    peekItem(uint256 const& id, SHAMapHash& hash) const;

    // traverse functions
    const_iterator
    upper_bound(uint256 const& id) const;

    /**  Visit every node in this SHAMap

         @param function called with every node visited.
         If function returns false, visitNodes exits.
    */
    void
    visitNodes(std::function<bool(SHAMapTreeNode&)> const& function) const;

    /**  Visit every node in this SHAMap that
         is not present in the specified SHAMap

         @param function called with every node visited.
         If function returns false, visitDifferences exits.
    */
    void
    visitDifferences(
        SHAMap const* have,
        std::function<bool(SHAMapTreeNode const&)>) const;

    /**  Visit every leaf node in this SHAMap

         @param function called with every non inner node visited.
    */
    void
    visitLeaves(
        std::function<void(std::shared_ptr<SHAMapItem const> const&)> const&)
        const;

    // comparison/sync functions

    /** Check for nodes in the SHAMap not available

        Traverse the SHAMap efficiently, maximizing I/O
        concurrency, to discover nodes referenced in the
        SHAMap but not available locally.

        @param maxNodes The maximum number of found nodes to return
        @param filter The filter to use when retrieving nodes
        @param return The nodes known to be missing
    */
    std::vector<std::pair<SHAMapNodeID, uint256>>
    getMissingNodes(int maxNodes, SHAMapSyncFilter* filter);

    bool
    getNodeFat(
        SHAMapNodeID const& wanted,
        std::vector<SHAMapNodeID>& nodeIDs,
        std::vector<Blob>& rawNodes,
        bool fatLeaves,
        std::uint32_t depth) const;

    /** Serializes the root in a format appropriate for sending over the wire */
    void
    serializeRoot(Serializer& s) const;

    SHAMapAddNode
    addRootNode(
        SHAMapHash const& hash,
        Slice const& rootNode,
        SHAMapSyncFilter* filter);
    SHAMapAddNode
    addKnownNode(
        SHAMapNodeID const& nodeID,
        Slice const& rawNode,
        SHAMapSyncFilter* filter);

    // status functions
    void
    setImmutable();
    bool
    isSynching() const;
    void
    setSynching();
    void
    clearSynching();
    bool
    isValid() const;

    // caution: otherMap must be accessed only by this function
    // return value: true=successfully completed, false=too different
    bool
    compare(SHAMap const& otherMap, Delta& differences, int maxCount) const;

    /** Convert any modified nodes to shared. */
    int
    unshare();

    /** Flush modified nodes to the nodestore and convert them to shared. */
    int
    flushDirty(NodeObjectType t);

    void
    walkMap(std::vector<SHAMapMissingNode>& missingNodes, int maxMissing) const;
    bool
    deepCompare(SHAMap& other) const;  // Intended for debug/test only

    void
    setUnbacked();

    void
    dump(bool withHashes = false) const;
    void
    invariants() const;

private:
    using SharedPtrNodeStack =
        std::stack<std::pair<std::shared_ptr<SHAMapTreeNode>, SHAMapNodeID>>;
    using DeltaRef = std::pair<
        std::shared_ptr<SHAMapItem const> const&,
        std::shared_ptr<SHAMapItem const> const&>;

    // tree node cache operations
    std::shared_ptr<SHAMapTreeNode>
    cacheLookup(SHAMapHash const& hash) const;
    void
    canonicalize(SHAMapHash const& hash, std::shared_ptr<SHAMapTreeNode>&)
        const;

    // database operations
    std::shared_ptr<SHAMapTreeNode>
    fetchNodeFromDB(SHAMapHash const& hash) const;
    std::shared_ptr<SHAMapTreeNode>
    fetchNodeNT(SHAMapHash const& hash) const;
    std::shared_ptr<SHAMapTreeNode>
    fetchNodeNT(SHAMapHash const& hash, SHAMapSyncFilter* filter) const;
    std::shared_ptr<SHAMapTreeNode>
    fetchNode(SHAMapHash const& hash) const;
    std::shared_ptr<SHAMapTreeNode>
    checkFilter(SHAMapHash const& hash, SHAMapSyncFilter* filter) const;

    /** Update hashes up to the root */
    void
    dirtyUp(
        SharedPtrNodeStack& stack,
        uint256 const& target,
        std::shared_ptr<SHAMapTreeNode> terminal);

    /** Walk towards the specified id, returning the node.  Caller must check
        if the return is nullptr, and if not, if the node->peekItem()->key() ==
       id */
    SHAMapLeafNode*
    walkTowardsKey(uint256 const& id, SharedPtrNodeStack* stack = nullptr)
        const;
    /** Return nullptr if key not found */
    SHAMapLeafNode*
    findKey(uint256 const& id) const;

    /** Unshare the node, allowing it to be modified */
    template <class Node>
    std::shared_ptr<Node>
    unshareNode(std::shared_ptr<Node>, SHAMapNodeID const& nodeID);

    /** prepare a node to be modified before flushing */
    template <class Node>
    std::shared_ptr<Node>
    preFlushNode(std::shared_ptr<Node> node) const;

    /** write and canonicalize modified node */
    std::shared_ptr<SHAMapTreeNode>
    writeNode(NodeObjectType t, std::shared_ptr<SHAMapTreeNode> node) const;

    SHAMapLeafNode*
    firstBelow(
        std::shared_ptr<SHAMapTreeNode>,
        SharedPtrNodeStack& stack,
        int branch = 0) const;

    // Simple descent
    // Get a child of the specified node
    SHAMapTreeNode*
    descend(SHAMapInnerNode*, int branch) const;
    SHAMapTreeNode*
    descendThrow(SHAMapInnerNode*, int branch) const;
    std::shared_ptr<SHAMapTreeNode>
    descend(std::shared_ptr<SHAMapInnerNode> const&, int branch) const;
    std::shared_ptr<SHAMapTreeNode>
    descendThrow(std::shared_ptr<SHAMapInnerNode> const&, int branch) const;

    // Descend with filter
    SHAMapTreeNode*
    descendAsync(
        SHAMapInnerNode* parent,
        int branch,
        SHAMapSyncFilter* filter,
        bool& pending) const;

    std::pair<SHAMapTreeNode*, SHAMapNodeID>
    descend(
        SHAMapInnerNode* parent,
        SHAMapNodeID const& parentID,
        int branch,
        SHAMapSyncFilter* filter) const;

    // Non-storing
    // Does not hook the returned node to its parent
    std::shared_ptr<SHAMapTreeNode>
    descendNoStore(std::shared_ptr<SHAMapInnerNode> const&, int branch) const;

    /** If there is only one leaf below this node, get its contents */
    std::shared_ptr<SHAMapItem const> const&
    onlyBelow(SHAMapTreeNode*) const;

    bool
    hasInnerNode(SHAMapNodeID const& nodeID, SHAMapHash const& hash) const;
    bool
    hasLeafNode(uint256 const& tag, SHAMapHash const& hash) const;

    SHAMapLeafNode const*
    peekFirstItem(SharedPtrNodeStack& stack) const;
    SHAMapLeafNode const*
    peekNextItem(uint256 const& id, SharedPtrNodeStack& stack) const;
    bool
    walkBranch(
        SHAMapTreeNode* node,
        std::shared_ptr<SHAMapItem const> const& otherMapItem,
        bool isFirstMap,
        Delta& differences,
        int& maxCount) const;
    int
    walkSubTree(bool doWrite, NodeObjectType t);

    // Structure to track information about call to
    // getMissingNodes while it's in progress
    struct MissingNodes
    {
        MissingNodes() = delete;
        MissingNodes(const MissingNodes&) = delete;
        MissingNodes&
        operator=(const MissingNodes&) = delete;

        // basic parameters
        int max_;
        SHAMapSyncFilter* filter_;
        int const maxDefer_;
        std::uint32_t generation_;

        // nodes we have discovered to be missing
        std::vector<std::pair<SHAMapNodeID, uint256>> missingNodes_;
        std::set<SHAMapHash> missingHashes_;

        // nodes we are in the process of traversing
        using StackEntry = std::tuple<
            SHAMapInnerNode*,  // pointer to the node
            SHAMapNodeID,      // the node's ID
            int,               // while child we check first
            int,               // which child we check next
            bool>;             // whether we've found any missing children yet

        // We explicitly choose to specify the use of std::deque here, because
        // we need to ensure that pointers and/or references to existing
        // elements will not be invalidated during the course of element
        // insertion and removal. Containers that do not offer this guarantee,
        // such as std::vector, can't be used here.
        std::stack<StackEntry, std::deque<StackEntry>> stack_;

        // nodes we may acquire from deferred reads
        std::vector<std::tuple<SHAMapInnerNode*, SHAMapNodeID, int>>
            deferredReads_;

        // nodes we need to resume after we get their children from deferred
        // reads
        std::map<SHAMapInnerNode*, SHAMapNodeID> resumes_;

        MissingNodes(
            int max,
            SHAMapSyncFilter* filter,
            int maxDefer,
            std::uint32_t generation)
            : max_(max)
            , filter_(filter)
            , maxDefer_(maxDefer)
            , generation_(generation)
        {
            missingNodes_.reserve(max);
            deferredReads_.reserve(maxDefer);
        }
    };

    // getMissingNodes helper functions
    void
    gmn_ProcessNodes(MissingNodes&, MissingNodes::StackEntry& node);
    void
    gmn_ProcessDeferredReads(MissingNodes&);
};

inline void
SHAMap::setFull()
{
    full_ = true;
}

inline void
SHAMap::setLedgerSeq(std::uint32_t lseq)
{
    ledgerSeq_ = lseq;
}

inline void
SHAMap::setImmutable()
{
    assert(state_ != SHAMapState::Invalid);
    state_ = SHAMapState::Immutable;
}

inline bool
SHAMap::isSynching() const
{
    return state_ == SHAMapState::Synching;
}

inline void
SHAMap::setSynching()
{
    state_ = SHAMapState::Synching;
}

inline void
SHAMap::clearSynching()
{
    state_ = SHAMapState::Modifying;
}

inline bool
SHAMap::isValid() const
{
    return state_ != SHAMapState::Invalid;
}

inline void
SHAMap::setUnbacked()
{
    backed_ = false;
}

//------------------------------------------------------------------------------

class SHAMap::const_iterator
{
public:
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = SHAMapItem;
    using reference = value_type const&;
    using pointer = value_type const*;

private:
    SharedPtrNodeStack stack_;
    SHAMap const* map_ = nullptr;
    pointer item_ = nullptr;

public:
    const_iterator() = default;

    reference
    operator*() const;
    pointer
    operator->() const;

    const_iterator&
    operator++();
    const_iterator
    operator++(int);

private:
    explicit const_iterator(SHAMap const* map);
    const_iterator(SHAMap const* map, pointer item);
    const_iterator(SHAMap const* map, pointer item, SharedPtrNodeStack&& stack);

    friend bool
    operator==(const_iterator const& x, const_iterator const& y);
    friend class SHAMap;
};

inline SHAMap::const_iterator::const_iterator(SHAMap const* map)
    : map_(map), item_(nullptr)
{
    auto temp = map_->peekFirstItem(stack_);
    if (temp)
        item_ = temp->peekItem().get();
}

inline SHAMap::const_iterator::const_iterator(SHAMap const* map, pointer item)
    : map_(map), item_(item)
{
}

inline SHAMap::const_iterator::const_iterator(
    SHAMap const* map,
    pointer item,
    SharedPtrNodeStack&& stack)
    : stack_(std::move(stack)), map_(map), item_(item)
{
}

inline SHAMap::const_iterator::reference
SHAMap::const_iterator::operator*() const
{
    return *item_;
}

inline SHAMap::const_iterator::pointer
SHAMap::const_iterator::operator->() const
{
    return item_;
}

inline SHAMap::const_iterator&
SHAMap::const_iterator::operator++()
{
    auto temp = map_->peekNextItem(item_->key(), stack_);
    if (temp)
        item_ = temp->peekItem().get();
    else
        item_ = nullptr;
    return *this;
}

inline SHAMap::const_iterator
SHAMap::const_iterator::operator++(int)
{
    auto tmp = *this;
    ++(*this);
    return tmp;
}

inline bool
operator==(SHAMap::const_iterator const& x, SHAMap::const_iterator const& y)
{
    assert(x.map_ == y.map_);
    return x.item_ == y.item_;
}

inline bool
operator!=(SHAMap::const_iterator const& x, SHAMap::const_iterator const& y)
{
    return !(x == y);
}

inline SHAMap::const_iterator
SHAMap::begin() const
{
    return const_iterator(this);
}

inline SHAMap::const_iterator
SHAMap::end() const
{
    return const_iterator(this, nullptr);
}

}  // namespace ripple

#endif
