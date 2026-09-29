#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/IntrusivePointer.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/Family.h>
#include <xrpl/shamap/SHAMapAddNode.h>
#include <xrpl/shamap/SHAMapInnerNode.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapLeafNode.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stack>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl {

class SHAMapSyncFilter;

/**
 * Describes the current state of a given SHAMap
 */
enum class SHAMapState : std::uint8_t {
    /**
     * The map is in flux and objects can be added and removed.
     *
     * Example: map underlying the open ledger.
     */
    Modifying = 0,

    /**
     * The map is set in stone and cannot be changed.
     *
     * Example: a map underlying a given closed ledger.
     */
    Immutable = 1,

    /**
     * The map's hash is fixed but valid nodes may be missing and can be added.
     *
     * Example: a map that's syncing a given peer's closing ledger.
     */
    Synching = 2,

    /**
     * The map is known to not be valid.
     *
     * Example: usually synching a corrupt ledger.
     */
    Invalid = 3,
};

/**
 * A SHAMap is both a radix tree with a fan-out of 16 and a Merkle tree.
 *
 * A radix tree is a tree with two properties:
 *
 *   1. The key for a node is represented by the node's position in the tree
 *      (the "prefix property").
 *   2. A node with only one child is merged with that child
 *      (the "merge property")
 *
 * These properties result in a significantly smaller memory footprint for
 * a radix tree.
 *
 * A fan-out of 16 means that each node in the tree has at most 16
 * children. See https://en.wikipedia.org/wiki/Radix_tree
 *
 * A Merkle tree is a tree where each non-leaf node is labelled with the hash
 * of the combined labels of its children nodes.
 *
 * A key property of a Merkle tree is that testing for node inclusion is
 * O(log(N)) where N is the number of nodes in the tree.
 *
 * See https://en.wikipedia.org/wiki/Merkle_tree
 */

/**
 * Holds a SHAMap node's identity, leaf status, and serialized data. Used by
 * getNodeFat to return node data for peer synchronization.
 */
struct SHAMapNodeData
{
    SHAMapNodeID nodeID;
    // The `data` field (a Blob, 8-byte aligned) needs 4 bytes of padding after the `nodeID` field
    // (36 bytes, 4-byte aligned) regardless of what comes between them, so `isLeaf` costs nothing
    // extra here. Moving it after `data` would add 8 bytes to the size of this struct instead.
    bool isLeaf;
    Blob data;
};

class SHAMap
{
private:
    Family& f_;
    beast::Journal journal_;

    /**
     * ID to distinguish this map for all others we're sharing nodes with.
     */
    std::uint32_t cowid_ = 1;

    // ledgerSeq_, state_ and full_ are touched on the nodestore fetch path and on a
    // getMissingNodes() walk, neither of which may block, so pin them lock-free.
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(std::atomic<SHAMapState>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);

    /**
     * The sequence of the ledger that this map references, if any.
     *
     * Written by Ledger::setFull() and by InboundLedger, and read on a
     * nodestore reader thread. Relaxed both ways, since it is only a lookup
     * hint for a store keyed by hash.
     */
    std::atomic<std::uint32_t> ledgerSeq_ = 0;

    SHAMapTreeNodePtr root_;

    /**
     * The map's state. Atomic because a getMissingNodes() walk writes it with
     * the acquisition's lock released. Mutable because a const descend() can
     * record the Invalid verdict.
     */
    mutable std::atomic<SHAMapState> state_;
    SHAMapType const type_;
    bool backed_ = true;  // Map is backed by the database

    /**
     * Map is believed complete in database.
     *
     * Atomic because finishFetch() clears it on any nodestore reader thread,
     * several of which can run at once.
     */
    mutable std::atomic<bool> full_ = false;

public:
    /**
     * Number of children each non-leaf node has (the 'radix tree' part of the
     * map)
     */
    static constexpr unsigned int kBranchFactor = SHAMapInnerNode::kBranchFactor;

    /**
     * The depth of the hash map: data is only present in the leaves
     */
    static constexpr unsigned int kLeafDepth = 64;

    using DeltaItem =
        std::pair<boost::intrusive_ptr<SHAMapItem const>, boost::intrusive_ptr<SHAMapItem const>>;
    using Delta = std::map<UInt256, DeltaItem>;

    SHAMap() = delete;
    SHAMap(SHAMap const&) = delete;
    SHAMap&
    operator=(SHAMap const&) = delete;

    /**
     * Take a snapshot of the given map.
     *
     * @param other The map to snapshot. An Invalid source yields an Invalid
     *        snapshot.
     * @param isMutable Whether the snapshot may be modified. Ignored when other
     *        is Invalid.
     */
    SHAMap(SHAMap const& other, bool isMutable);

    // build new map
    SHAMap(SHAMapType t, Family& f);

    SHAMap(SHAMapType t, UInt256 const& hash, Family& f);

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

    /**
     * Iterator to a SHAMap's leaves
     * This is always a const iterator.
     * Meets the requirements of ForwardRange.
     */
    class ConstIterator;

    ConstIterator
    begin() const;
    ConstIterator
    end() const;

    //--------------------------------------------------------------------------

    /**
     * Return a new map that is a snapshot of this one.
     *
     * Handles copy on write for mutable snapshots. An invalid map yields an
     * invalid snapshot.
     *
     * @param isMutable Whether the snapshot may be modified.
     * @return The snapshot.
     */
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
    fetchRoot(SHAMapHash const& hash, SHAMapSyncFilter const* filter);

    // normal hash access functions

    /**
     * Does the tree have an item with the given ID?
     */
    bool
    hasItem(UInt256 const& id) const;

    bool
    delItem(UInt256 const& id);

    bool
    addItem(SHAMapNodeType type, boost::intrusive_ptr<SHAMapItem const> item);

    SHAMapHash
    getHash() const;

    // save a copy if you have a temporary anyway
    bool
    updateGiveItem(SHAMapNodeType type, boost::intrusive_ptr<SHAMapItem const> item);

    bool
    addGiveItem(SHAMapNodeType type, boost::intrusive_ptr<SHAMapItem const> item);

    // Save a copy if you need to extend the life
    // of the SHAMapItem beyond this SHAMap
    boost::intrusive_ptr<SHAMapItem const> const&
    peekItem(UInt256 const& id) const;
    boost::intrusive_ptr<SHAMapItem const> const&
    peekItem(UInt256 const& id, SHAMapHash& hash) const;

    // traverse functions
    /**
     * Find the first item after the given item.
     *
     * @param id the identifier of the item, which need not exist in the map.
     * @return an iterator at the first item with a greater key, or end() if the
     *         map holds no greater key.
     * @throws SHAMapMissingNode if the map cannot be walked. end() is the
     *         separate answer that the map holds no greater key.
     */
    ConstIterator
    upperBound(UInt256 const& id) const;

    /**
     * Find the object with the greatest object id smaller than the input id.
     *
     * @param id the identifier of the item, which need not exist in the map.
     * @return an iterator at the last item with a smaller key, or end() if the
     *         map holds no smaller key.
     * @throws SHAMapMissingNode if the map cannot be walked. end() is the
     *         separate answer that the map holds no smaller key.
     */
    ConstIterator
    lowerBound(UInt256 const& id) const;

    /**
     * Visit every node in this SHAMap
     *
     * @param function called with every node visited.
     * If function returns false, visitNodes exits.
     */
    void
    visitNodes(std::function<bool(SHAMapTreeNode&)> const& function) const;

    /**
     * Visit every node in this SHAMap that
     * is not present in the specified SHAMap
     *
     * @param function called with every node visited.
     * If function returns false, visitDifferences exits.
     */
    void
    visitDifferences(SHAMap const* have, std::function<bool(SHAMapTreeNode const&)> const&) const;

    /**
     * Visit every leaf node in this SHAMap
     *
     * @param function called with every non inner node visited.
     */
    void
    visitLeaves(std::function<void(boost::intrusive_ptr<SHAMapItem const> const&)> const&) const;

    // comparison/sync functions

    /**
     * Check for nodes in the SHAMap not available
     *
     * Traverse the SHAMap efficiently, maximizing I/O
     * concurrency, to discover nodes referenced in the
     * SHAMap but not available locally.
     *
     * Only a leaf may occupy a position at or beyond kLeafDepth. A map that
     * breaks that is marked Invalid and the traversal is abandoned, so callers
     * ask isValid() to tell an empty result from a satisfied map.
     *
     * @param maxNodes The maximum number of found nodes to return
     * @param filter The filter to use when retrieving nodes
     * @return The nodes known to be missing, or empty if the map is Invalid
     */
    std::vector<std::pair<SHAMapNodeID, UInt256>>
    getMissingNodes(int maxNodes, SHAMapSyncFilter const* filter);

    [[nodiscard]] bool
    getNodeFat(
        SHAMapNodeID const& wanted,
        std::vector<SHAMapNodeData>& data,
        bool fatLeaves,
        std::uint32_t depth) const;

    /**
     * Get the proof path of the key. The proof path is every node on the path
     * from leaf to root. Sibling hashes are stored in the parent nodes.
     * @param key  key of the leaf
     * @return the proof path if found
     */
    std::optional<std::vector<Blob>>
    getProofPath(UInt256 const& key) const;

    /**
     * Verify the proof path
     * @param rootHash  root hash of the map
     * @param key  key of the leaf
     * @param path  the proof path
     * @return true if verified successfully
     */
    static bool
    verifyProofPath(UInt256 const& rootHash, UInt256 const& key, std::vector<Blob> const& path);

    /**
     * Serializes the root in a format appropriate for sending over the wire
     */
    void
    serializeRoot(Serializer& s) const;

    /**
     * Add a root node to the SHAMap during synchronization.
     *
     * This function is used when receiving the root node of a SHAMap from a peer during ledger
     * synchronization. The node must already have been deserialized.
     *
     * A root offered under a hash the map does not hold names another tree and
     * is reported as invalid data. A root matching that hash is a duplicate.
     *
     * @param hash The expected hash of the root node.
     * @param rootNode A deserialized root node to add.
     * @param filter Optional sync filter to track received nodes.
     * @return Status indicating whether the node was useful, duplicate, or invalid.
     *
     * @note This function expects the rootNode to be a valid, deserialized SHAMapTreeNode. The
     *       caller is responsible for deserialization and basic validation before calling this
     *       function.
     */
    SHAMapAddNode
    addRootNode(SHAMapHash const& hash, SHAMapTreeNodePtr rootNode, SHAMapSyncFilter const* filter);

    /**
     * Add a known node at a specific position in the SHAMap during synchronization.
     *
     * This function is used when receiving nodes from peers during ledger synchronization. The node
     * is inserted at the position specified by nodeID. The node must already have been
     * deserialized.
     *
     * A node that no valid tree can hold makes the map Invalid, which is
     * terminal. An acquisition reaching that verdict gives up.
     *
     * @param nodeID The position in the tree where this node belongs.
     * @param treeNode A deserialized tree node to add.
     * @param filter Optional sync filter to track received nodes.
     * @return Status indicating whether the node was useful, duplicate, or invalid.
     *
     * @note The caller is responsible for deserialization. The position is
     *       checked here, in three steps that share two verdicts. A node
     *       the descent reaches at a position other than the one nodeID
     *       claims is refused with invalid(), and the map stays valid, since
     *       only the label was wrong. A leaf that hash-verified at the
     *       position nodeID claims, but whose own key does not lie under
     *       nodeID, condemns the map and returns mapInvalidated(). A node the
     *       descent itself condemns on the way, resolved from the local store
     *       or the filter rather than supplied by the caller, leaves the map
     *       Invalid and returns invalid(), since that verdict is not the
     *       caller's doing. Any map outside Synching, including one already
     *       condemned, returns duplicate().
     */
    SHAMapAddNode
    addKnownNode(
        SHAMapNodeID const& nodeID,
        SHAMapTreeNodePtr treeNode,
        SHAMapSyncFilter const* filter);

    /**
     * Mark this map as immutable, so it can no longer be modified.
     *
     * @return false if the map is Invalid and was left unchanged, true
     *         otherwise.
     */
    [[nodiscard]] bool
    setImmutable();

    /**
     * @return Whether the map is being synced against a hash it was given, so
     *         its hash is fixed while nodes may still be added.
     */
    [[nodiscard]] bool
    isSynching() const;

    /**
     * @return Whether the map is settled, with its hash and nodes fixed.
     */
    [[nodiscard]] bool
    isImmutable() const;

    /**
     * Mark this map as syncing, fixing its hash while still allowing missing
     * nodes to be added.
     *
     * The map must be freshly constructed. The body asserts that the map is not
     * Invalid, so a caller that ignores the precondition stops a build with
     * assertions enabled.
     */
    void
    setSynching();

    /**
     * Mark this map as no longer syncing, so it can be modified again.
     *
     * Moves only a Synching map. An Immutable map stays settled, a Modifying
     * map has nothing to clear, and Invalid is terminal.
     */
    void
    clearSynching();

    /**
     * Whether the map can still be the map it claims to be.
     *
     * A map that is merely missing nodes is valid, and stays valid until
     * something proves the tree it is syncing against cannot exist.
     *
     * @return Whether the map has not been proven impossible.
     */
    [[nodiscard]] bool
    isValid() const;

    // caution: otherMap must be accessed only by this function
    // return value: true=successfully completed, false=too different
    bool
    compare(SHAMap const& otherMap, Delta& differences, int maxCount) const;

    /**
     * Convert any modified nodes to shared.
     */
    int
    unshare();

    /**
     * Flush modified nodes to the nodestore and convert them to shared.
     */
    int
    flushDirty(NodeObjectType t);

    void
    walkMap(std::vector<SHAMapMissingNode>& missingNodes, int maxMissing) const;
    bool
    walkMapParallel(std::vector<SHAMapMissingNode>& missingNodes, int maxMissing) const;
    bool
    deepCompare(SHAMap& other) const;  // Intended for debug/test only

    void
    setUnbacked();

    void
    dump(bool withHashes = false) const;
    void
    invariants() const;

private:
    /**
     * Whether placing `node` one level below `parentDepth` leaves it no room.
     *
     * Only a leaf may sit at kLeafDepth, and only a parent above kLeafDepth has
     * a nibble left to record the branch taken. Both bounds are needed, since a
     * leaf one level above kLeafDepth is allowed. Position is judged separately,
     * by the walk that keeps a path.
     *
     * The conjunct order is load bearing: the depth is tested first, so the
     * virtual isInner() call runs only at the one level where the bound
     * applies.
     *
     * @param parentDepth the depth of the node being descended from.
     * @param node the node about to be placed one level below it.
     * @return whether that placement is past the deepest level this kind of
     *         node may occupy.
     */
    [[nodiscard]] static bool
    pastLeafDepth(unsigned int parentDepth, SHAMapTreeNode const& node)
    {
        return parentDepth + 1u >= kLeafDepth && (node.isInner() || parentDepth >= kLeafDepth);
    }

    /**
     * A path from the root of the map down to some node, pairing each node with the ID naming its
     * position.
     *
     * The two halves of an entry must agree, and the only way to get that wrong is to compute an ID
     * from the wrong branch. So this type does not accept an ID at all: every push takes the branch
     * being descended and derives the ID itself, so a node and its ID cannot disagree. Reads are
     * exposed through the same accessors a std::stack would offer.
     */
    class NodePathStack
    {
    public:
        [[nodiscard]] bool
        empty() const
        {
            return stack_.empty();
        }

        [[nodiscard]] std::size_t
        size() const
        {
            return stack_.size();
        }

        /**
         * The node at the end of the path, paired with its ID.
         *
         * std::stack::top on an empty stack is undefined, so an empty path
         * yields a null node the caller can test. The assert alone is
         * stripped in release.
         */
        [[nodiscard]] std::pair<SHAMapTreeNodePtr, SHAMapNodeID> const&
        top() const
        {
            if (stack_.empty())
            {
                // LCOV_EXCL_START
                UNREACHABLE("xrpl::SHAMap::NodePathStack::top : empty stack");
                static std::pair<SHAMapTreeNodePtr, SHAMapNodeID> const kEmpty;
                return kEmpty;
                // LCOV_EXCL_STOP
            }
            return stack_.top();
        }

        /**
         * Shorten the path by one node. An empty path is left as it is.
         */
        void
        pop()
        {
            if (stack_.empty())
            {
                // LCOV_EXCL_START
                UNREACHABLE("xrpl::SHAMap::NodePathStack::pop : empty stack");
                return;
                // LCOV_EXCL_STOP
            }
            stack_.pop();
        }

        /**
         * Discard the whole path.
         */
        void
        clear()
        {
            stack_ = {};
        }

        /**
         * Start a path at the root of the map, whose ID is the zero-depth ID by definition.
         *
         * @return false, leaving the path unchanged, if a path was already
         *         started, so the caller stops rather than overwriting it.
         */
        [[nodiscard]] bool
        pushRoot(SHAMapTreeNodePtr node)
        {
            if (!stack_.empty())
            {
                // LCOV_EXCL_START
                UNREACHABLE("xrpl::SHAMap::NodePathStack::pushRoot : non-empty stack");
                return false;
                // LCOV_EXCL_STOP
            }
            stack_.emplace(std::move(node), SHAMapNodeID{});
            return true;
        }

        /**
         * Extend the path to the child of the current node reached by `branch`.
         *
         * A node keeps the depth it was reached at.
         *
         * @param node the child to append.
         * @param branch the branch of the current node that `node` was
         *               reached through.
         * @return false, leaving the path unchanged, if there is no node to
         *         descend from, no node to push, no branch of that number, no
         *         room left below for the kind of node offered, or a leaf
         *         whose own key does not lie under `branch`. The caller stops
         *         walking on a false return.
         */
        [[nodiscard]] bool
        pushChild(SHAMapTreeNodePtr node, unsigned int branch)
        {
            if (stack_.empty() || !node || branch >= kBranchFactor)
            {
                // LCOV_EXCL_START
                UNREACHABLE("xrpl::SHAMap::NodePathStack::pushChild : no child to push");
                return false;
                // LCOV_EXCL_STOP
            }

            // Reachable, for the same reason the misplaced-leaf case below is: the two-argument
            // SHAMap::descend fetches by the parent's recorded child hash and hooks what comes
            // back, and a parsed node adopts that hash, so an inner node can arrive one level
            // too deep. The push is refused and the caller stops.
            auto const& parentID = stack_.top().second;
            auto const parentDepth = parentID.getDepth();
            bool const tooDeep = pastLeafDepth(parentDepth, *node);
            SOMETIMES(tooDeep, "xrpl::SHAMap::NodePathStack::pushChild : child past leaf depth");
            if (tooDeep)
            {
                return false;
            }

            // A leaf's own key names its position, so a leaf reached by this branch must agree with
            // the ID that branch derives. Where the two disagree the push is refused.
            //
            // Reported rather than asserted, for the reason given above: a map read lazily from
            // the local store reaches this push directly, with no hook ahead of it to judge the
            // node first.
            auto childID = parentID.getChildNodeID(branch);
            bool const misplaced = !belongsAt(childID, *node);
            SOMETIMES(
                misplaced, "xrpl::SHAMap::NodePathStack::pushChild : leaf key outside branch");
            if (misplaced)
            {
                return false;
            }

            stack_.emplace(std::move(node), std::move(childID));
            return true;
        }

        /**
         * Extend the path to a node lying on the path to `target`.
         *
         * For nodes not reached by descending a known branch: the walk tracks only the key it is
         * heading for, or the node is newly created. Either way `target` selects the branch.
         */
        [[nodiscard]] bool
        pushNode(SHAMapTreeNodePtr node, UInt256 const& target)
        {
            if (stack_.empty())
            {
                return pushRoot(std::move(node));
            }
            return pushChild(std::move(node), selectBranch(stack_.top().second, target));
        }

    private:
        std::stack<std::pair<SHAMapTreeNodePtr, SHAMapNodeID>> stack_;
    };

    using DeltaRef =
        std::pair<boost::intrusive_ptr<SHAMapItem const>, boost::intrusive_ptr<SHAMapItem const>>;

    /**
     * The sequence of the ledger this map references, read atomically.
     *
     * @return The sequence, or zero if the map references no ledger.
     */
    [[nodiscard]] std::uint32_t
    ledgerSeq() const;

    /**
     * The current state, read atomically.
     *
     * Orders state_ alone. The tree's nodes carry no ordering guarantees.
     *
     * @return The state as of the call, which a concurrent walk may already
     *         have moved past.
     */
    [[nodiscard]] SHAMapState
    state() const;

    /**
     * Record that the map is provably not the one it claims to be.
     *
     * Cannot fail, since Invalid outranks every other state. Const because a
     * const descend() can reach this verdict while resolving a node.
     */
    void
    setInvalid() const;

    /**
     * Condemn the map, naming in the log the node whose position proves it.
     *
     * Const, because a read path can reach this verdict. See setInvalid().
     *
     * @param node the node that cannot sit where it was offered.
     * @param hash the hash it was resolved under.
     * @param position the place in the tree it was offered for.
     */
    void
    condemn(SHAMapTreeNode const& node, SHAMapHash const& hash, SHAMapNodeID const& position) const;

    /**
     * Move the map to a new state, atomically.
     *
     * With clearSynching(), the only writer of state_ past construction.
     * Invalid is always stored, and every other transition is refused once
     * the map is Invalid, which is what makes that verdict terminal.
     * clearSynching() is narrower and moves only Synching.
     *
     * Const to let setInvalid() be const. state_ is mutable, which makes that
     * legal.
     *
     * @param desired The state to move to.
     * @return false if the map is Invalid and the requested state is not,
     *         leaving it unchanged. True otherwise.
     */
    bool
    trySetState(SHAMapState desired) const;

    // tree node cache operations
    SHAMapTreeNodePtr
    cacheLookup(SHAMapHash const& hash) const;

    void
    canonicalize(SHAMapHash const& hash, SHAMapTreeNodePtr&) const;

    // database operations
    SHAMapTreeNodePtr
    fetchNodeFromDB(SHAMapHash const& hash) const;
    SHAMapTreeNodePtr
    fetchNodeNT(SHAMapHash const& hash) const;
    SHAMapTreeNodePtr
    fetchNodeNT(SHAMapHash const& hash, SHAMapSyncFilter const* filter) const;
    SHAMapTreeNodePtr
    fetchNode(SHAMapHash const& hash) const;
    SHAMapTreeNodePtr
    checkFilter(SHAMapHash const& hash, SHAMapSyncFilter const* filter) const;

    /**
     * Update hashes up to the root
     */
    void
    dirtyUp(NodePathStack& stack, UInt256 const& target, SHAMapTreeNodePtr terminal);

    /**
     * Walk towards the specified id, returning the node.  Caller must check
     *  if the return is nullptr, and if not, if the node->peekItem()->key() ==
     * id
     */
    SHAMapLeafNode*
    walkTowardsKey(UInt256 const& id, NodePathStack* stack = nullptr) const;
    /**
     * Return nullptr if key not found
     */
    SHAMapLeafNode*
    findKey(UInt256 const& id) const;

    /**
     * Unshare the node, allowing it to be modified
     */
    template <class Node>
    intr_ptr::SharedPtr<Node>
    unshareNode(intr_ptr::SharedPtr<Node>, SHAMapNodeID const& nodeID);

    /**
     * prepare a node to be modified before flushing
     */
    template <class Node>
    intr_ptr::SharedPtr<Node>
    preFlushNode(intr_ptr::SharedPtr<Node> node) const;

    /**
     * write and canonicalize modified node
     */
    SHAMapTreeNodePtr
    writeNode(NodeObjectType t, SHAMapTreeNodePtr node) const;

    // direction in which a scan walks an inner node's branches
    enum class BelowDirection { First, Last };

    /**
     * Returns the first or last item at or below the node already on top of `stack`, extending
     * `stack` with the path walked to reach it.
     *
     * @param stack the path to extend, whose last node the search starts from.
     * @param direction whether to take the lowest or the highest branch at
     *                  each level.
     * @return the leaf found, or nullptr if no leaf lies below that node.
     */
    [[nodiscard]] SHAMapLeafNode*
    belowHelper(NodePathStack& stack, BelowDirection direction) const;

    /**
     * Returns the nearest item strictly past `id`, in the given direction.
     *
     * Walks back up the path to `id`. At each inner node the branches beyond the one `id` takes
     * hold the candidates, so the first non-empty one is the closest and the extreme leaf below
     * it is the answer.
     *
     * @param id The key to search from, which need not be in the map.
     * @param direction First to search upwards from `id`, Last to search downwards.
     * @return An iterator to the item found, or end() if no item lies on that side of `id`.
     */
    [[nodiscard]] ConstIterator
    boundHelper(UInt256 const& id, BelowDirection direction) const;

    // Simple descent
    // Get a child of the specified node
    SHAMapTreeNode*
    descend(SHAMapInnerNode*, unsigned int branch) const;
    SHAMapTreeNode*
    descendThrow(SHAMapInnerNode*, unsigned int branch) const;
    SHAMapTreeNodePtr
    descend(SHAMapInnerNode&, unsigned int branch) const;
    SHAMapTreeNodePtr
    descendThrow(SHAMapInnerNode&, unsigned int branch) const;

    // Descend with filter
    // If pending, callback is called as if it called fetchNodeNT
    using DescendCallback = std::function<void(SHAMapTreeNodePtr, SHAMapHash const&)>;
    SHAMapTreeNode*
    descendAsync(
        SHAMapInnerNode* parent,
        unsigned int branch,
        SHAMapSyncFilter const* filter,
        bool& pending,
        DescendCallback&&) const;

    /**
     * Resolve the child of `parent` on `branch`, judging where it lands.
     *
     * Const, yet it records the Invalid verdict through setInvalid(). A new
     * caller must therefore be a path that may reach that verdict. Today
     * addKnownNode() is the only one.
     *
     * @param parent the inner node to descend from.
     * @param parentID the position of `parent`.
     * @param branch the branch of `parent` to resolve, which the caller has
     *               already bounded.
     * @param filter an alternate source of nodes, or null for the store alone.
     * @return the child and the child's position. A null child where the branch
     *         could not be resolved, or where what came back does not belong at
     *         that position, in which case the map is left Invalid.
     */
    std::pair<SHAMapTreeNode*, SHAMapNodeID>
    descend(
        SHAMapInnerNode* parent,
        SHAMapNodeID const& parentID,
        unsigned int branch,
        SHAMapSyncFilter const* filter) const;

    // Non-storing
    // Does not hook the returned node to its parent
    SHAMapTreeNodePtr
    descendNoStore(SHAMapInnerNode&, unsigned int branch) const;

    /**
     * If there is only one leaf below this node, get its contents
     */
    boost::intrusive_ptr<SHAMapItem const> const&
    onlyBelow(SHAMapTreeNode*) const;

    bool
    hasInnerNode(SHAMapNodeID const& nodeID, SHAMapHash const& hash) const;
    bool
    hasLeafNode(UInt256 const& tag, SHAMapHash const& hash) const;

    SHAMapLeafNode const*
    peekFirstItem(NodePathStack& stack) const;
    SHAMapLeafNode const*
    peekNextItem(UInt256 const& id, NodePathStack& stack) const;
    bool
    walkBranch(
        SHAMapTreeNode* node,
        boost::intrusive_ptr<SHAMapItem const> const& otherMapItem,
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
        MissingNodes(MissingNodes const&) = delete;
        MissingNodes&
        operator=(MissingNodes const&) = delete;

        // basic parameters
        int max;
        SHAMapSyncFilter const* filter;
        int const maxDefer;
        std::uint32_t generation;

        // nodes we have discovered to be missing
        std::vector<std::pair<SHAMapNodeID, UInt256>> missingNodes;
        std::set<SHAMapHash> missingHashes;

        // nodes we are in the process of traversing
        using StackEntry = std::tuple<
            SHAMapInnerNode*,  // pointer to the node
            SHAMapNodeID,      // the node's ID
            unsigned int,      // which child we check first
            unsigned int,      // which child we check next
            bool>;             // whether we've found any missing children yet

        // We explicitly choose to specify the use of std::deque here, because
        // we need to ensure that pointers and/or references to existing
        // elements will not be invalidated during the course of element
        // insertion and removal. Containers that do not offer this guarantee,
        // such as std::vector, can't be used here.
        std::stack<StackEntry, std::deque<StackEntry>> stack;

        // nodes we may have acquired from deferred reads
        using DeferredNode = std::tuple<
            SHAMapInnerNode*,    // parent node
            SHAMapNodeID,        // parent node ID
            unsigned int,        // branch
            SHAMapTreeNodePtr>;  // node

        int deferred;
        std::mutex deferLock;
        std::condition_variable deferCondVar;
        std::vector<DeferredNode> finishedReads;

        // nodes we need to resume after we get their children from deferred
        // reads
        std::map<SHAMapInnerNode*, SHAMapNodeID> resumes;

        MissingNodes(
            int max,
            SHAMapSyncFilter const* filter,
            int maxDefer,
            std::uint32_t generation)
            : max(max), filter(filter), maxDefer(maxDefer), generation(generation), deferred(0)
        {
            missingNodes.reserve(max);
            finishedReads.reserve(maxDefer);
        }
    };

    // getMissingNodes helper functions

    /**
     * Examine the remaining branches of one inner node, recording or
     * requesting what is missing.
     *
     * @param mn the walk's shared state, which collects the missing nodes.
     * @param node the walk's current position, updated to the node to process
     *             next.
     */
    void
    gmnProcessNodes(MissingNodes& mn, MissingNodes::StackEntry& node);

    /**
     * Wait for every read this pass posted, then hook up or record what each
     * one resolved.
     *
     * Drains all of them even after judging the map, since an outstanding read
     * holds a pointer to `mn`.
     *
     * @param mn the walk's shared state, holding the posted reads.
     */
    void
    gmnProcessDeferredReads(MissingNodes& mn);

    // fetch from DB helper function
    SHAMapTreeNodePtr
    finishFetch(SHAMapHash const& hash, std::shared_ptr<NodeObject> const& object) const;
};

inline void
SHAMap::setFull()
{
    full_.store(true, std::memory_order_release);
}

inline void
SHAMap::setLedgerSeq(std::uint32_t lseq)
{
    ledgerSeq_.store(lseq, std::memory_order_relaxed);
}

inline std::uint32_t
SHAMap::ledgerSeq() const
{
    return ledgerSeq_.load(std::memory_order_relaxed);
}

inline SHAMapState
SHAMap::state() const
{
    return state_.load(std::memory_order_acquire);
}

inline bool
SHAMap::trySetState(SHAMapState desired) const
{
    // Invalid is stored outright: that verdict outranks a concurrent transition.
    if (desired == SHAMapState::Invalid)
    {
        state_.store(SHAMapState::Invalid, std::memory_order_release);
        return true;
    }

    // A failed exchange both reports the state and refreshes expected, so no load is needed
    // ahead of the loop.
    auto expected = SHAMapState::Modifying;
    while (expected != SHAMapState::Invalid)
    {
        if (state_.compare_exchange_weak(
                expected, desired, std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return true;
        }
    }
    return false;
}

inline bool
SHAMap::setImmutable()
{
    SOMETIMES(!isValid(), "xrpl::SHAMap::setImmutable : map is invalid");
    return trySetState(SHAMapState::Immutable);
}

inline bool
SHAMap::isSynching() const
{
    return state() == SHAMapState::Synching;
}

inline bool
SHAMap::isImmutable() const
{
    return state() == SHAMapState::Immutable;
}

inline void
SHAMap::setSynching()
{
    // Guarded, so this is not a way out of Invalid.
    if (!trySetState(SHAMapState::Synching))
    {
        // Only ever called on a freshly constructed map.
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::SHAMap::setSynching : map is invalid");
        // LCOV_EXCL_STOP
    }
}

inline void
SHAMap::clearSynching()
{
    // Only Synching moves. A walk can end after the ledger settled, and then the map stays
    // Immutable; a Modifying map has nothing to clear; an invalid map stays invalid. Only that
    // last refusal is logged, since a concurrent walk is what produces it.
    auto expected = SHAMapState::Synching;
    if (state_.compare_exchange_strong(
            expected, SHAMapState::Modifying, std::memory_order_acq_rel, std::memory_order_acquire))
    {
        return;
    }

    bool const invalid = expected == SHAMapState::Invalid;
    SOMETIMES(invalid, "xrpl::SHAMap::clearSynching : map is invalid");
    if (invalid)
    {
        JLOG(journal_.warn()) << "Refused to clear synching on an invalid map, root hash "
                              << root_->getHash();
    }
}

inline bool
SHAMap::isValid() const
{
    return state() != SHAMapState::Invalid;
}

inline void
SHAMap::setInvalid() const
{
    // Through trySetState() like every other transition, so state_ has one writer funnel.
    trySetState(SHAMapState::Invalid);
}

inline void
SHAMap::setUnbacked()
{
    backed_ = false;
}

//------------------------------------------------------------------------------

class SHAMap::ConstIterator
{
public:
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = SHAMapItem;
    using reference = value_type const&;
    using pointer = value_type const*;

private:
    NodePathStack stack_;
    SHAMap const* map_ = nullptr;
    pointer item_ = nullptr;

public:
    ConstIterator() = delete;

    ConstIterator(ConstIterator const& other) = default;
    ConstIterator&
    operator=(ConstIterator const& other) = default;

    ~ConstIterator() = default;

    reference
    operator*() const;
    pointer
    operator->() const;

    ConstIterator&
    operator++();
    ConstIterator
    operator++(int);

private:
    explicit ConstIterator(SHAMap const* map);
    ConstIterator(SHAMap const* map, std::nullptr_t);
    ConstIterator(SHAMap const* map, pointer item, NodePathStack&& stack);

    friend bool
    operator==(ConstIterator const& x, ConstIterator const& y);
    friend class SHAMap;
};

inline SHAMap::ConstIterator::ConstIterator(SHAMap const* map) : map_(map)
{
    XRPL_ASSERT(map_, "xrpl::SHAMap::ConstIterator::ConstIterator : non-null input");

    if (auto temp = map_->peekFirstItem(stack_))
        item_ = temp->peekItem().get();
}

inline SHAMap::ConstIterator::ConstIterator(SHAMap const* map, std::nullptr_t) : map_(map)
{
}

inline SHAMap::ConstIterator::ConstIterator(SHAMap const* map, pointer item, NodePathStack&& stack)
    : stack_(std::move(stack)), map_(map), item_(item)
{
}

inline SHAMap::ConstIterator::reference
SHAMap::ConstIterator::operator*() const
{
    return *item_;
}

inline SHAMap::ConstIterator::pointer
SHAMap::ConstIterator::operator->() const
{
    return item_;
}

inline SHAMap::ConstIterator&
SHAMap::ConstIterator::operator++()
{
    if (auto temp = map_->peekNextItem(item_->key(), stack_))
    {
        item_ = temp->peekItem().get();
    }
    else
    {
        item_ = nullptr;
    }
    return *this;
}

inline SHAMap::ConstIterator
SHAMap::ConstIterator::operator++(int)
{
    auto tmp = *this;
    ++(*this);
    return tmp;
}

inline bool
operator==(SHAMap::ConstIterator const& x, SHAMap::ConstIterator const& y)
{
    XRPL_ASSERT(
        x.map_ == y.map_,
        "xrpl::operator==(SHAMap::const_iterator, SHAMap::const_iterator) : "
        "inputs map do match");
    return x.item_ == y.item_;
}

inline SHAMap::ConstIterator
SHAMap::begin() const
{
    return ConstIterator(this);
}

inline SHAMap::ConstIterator
SHAMap::end() const
{
    return ConstIterator(this, nullptr);
}

}  // namespace xrpl
