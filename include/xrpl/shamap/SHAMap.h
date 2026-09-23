#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/IntrusivePointer.h>
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
enum class SHAMapState {
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
 * A SHAMap is both a trie with a fan-out of 16 and a Merkle tree.
 *
 * A trie keeps a key in the position of its nodes rather than in the nodes
 * themselves: the path from the root down to a node spells out the prefix
 * every key below it shares (the "prefix property"). A SHAMap spends one
 * nibble of the 256-bit key per level, so each inner node has at most 16
 * children, which is the fan-out, and a leaf sits at depth 64 at the deepest.
 * A leaf also carries its own full key, which is what lets a reader check
 * that it was reached through the branches that key names.
 *
 * A radix tree adds a second property: a node with only one child is merged
 * with that child (the "merge property"), which is what makes it a compressed
 * trie. A SHAMap does not maintain that, so it is a trie and not a radix
 * tree. Adding an item creates an inner node at every nibble the two keys
 * share, however long that run is, and never merges one away. Deleting does
 * merge: a chain that reduces to a single leaf collapses, pulling that leaf
 * up to one nibble below the nearest ancestor still holding two branches.
 * Either way no edge spans more than one nibble, so two keys agreeing on
 * their first 63 nibbles give 63 single-child inner nodes, an inner node at
 * depth 63 holding both branches, and the two leaves at depth 64: one entry
 * per level with no gaps, and 65 entries at the most. Traversal relies on
 * that, since it is what makes a path's length name each node's depth.
 *
 * See https://en.wikipedia.org/wiki/Trie and
 * https://en.wikipedia.org/wiki/Radix_tree
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

    /**
     * The sequence of the ledger that this map references, if any.
     */
    std::uint32_t ledgerSeq_ = 0;

    SHAMapTreeNodePtr root_;
    mutable SHAMapState state_;
    SHAMapType const type_;
    bool backed_ = true;         // Map is backed by the database
    mutable bool full_ = false;  // Map is believed complete in database

public:
    /**
     * Number of children each non-leaf node has, which is the trie's fan-out
     */
    static constexpr unsigned int kBranchFactor = SHAMapInnerNode::kBranchFactor;

    /**
     * The depth of the hash map: data is only present in the leaves
     */
    static constexpr unsigned int kLeafDepth = 64;

    using DeltaItem =
        std::pair<boost::intrusive_ptr<SHAMapItem const>, boost::intrusive_ptr<SHAMapItem const>>;
    using Delta = std::map<uint256, DeltaItem>;

    SHAMap() = delete;
    SHAMap(SHAMap const&) = delete;
    SHAMap&
    operator=(SHAMap const&) = delete;

    // Take a snapshot of the given map:
    SHAMap(SHAMap const& other, bool isMutable);

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
    fetchRoot(SHAMapHash const& hash, SHAMapSyncFilter const* filter);

    // normal hash access functions

    /**
     * Does the tree have an item with the given ID?
     */
    bool
    hasItem(uint256 const& id) const;

    bool
    delItem(uint256 const& id);

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
    peekItem(uint256 const& id) const;
    boost::intrusive_ptr<SHAMapItem const> const&
    peekItem(uint256 const& id, SHAMapHash& hash) const;

    // traverse functions
    /**
     * Find the first item after the given item.
     *
     * @param id the identifier of the item.
     *
     * @note The item does not need to exist.
     */
    ConstIterator
    upperBound(uint256 const& id) const;

    /**
     * Find the object with the greatest object id smaller than the input id.
     *
     * @param id the identifier of the item.
     *
     * @note The item does not need to exist.
     */
    ConstIterator
    lowerBound(uint256 const& id) const;

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
     * @param maxNodes The maximum number of found nodes to return
     * @param filter The filter to use when retrieving nodes
     * @param return The nodes known to be missing
     */
    std::vector<std::pair<SHAMapNodeID, uint256>>
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
    getProofPath(uint256 const& key) const;

    /**
     * Verify the proof path
     * @param rootHash  root hash of the map
     * @param key  key of the leaf
     * @param path  the proof path
     * @return true if verified successfully
     */
    static bool
    verifyProofPath(uint256 const& rootHash, uint256 const& key, std::vector<Blob> const& path);

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
     * @param nodeID The position in the tree where this node belongs.
     * @param treeNode A deserialized tree node to add.
     * @param filter Optional sync filter to track received nodes.
     * @return Status indicating whether the node was useful, duplicate, or invalid.
     *
     * @note This function expects the treeNode to be a valid, deserialized SHAMapTreeNode. The
     *       caller is responsible for deserialization and basic validation before calling this
     *       function. This also means that the nodeID must be consistent with the node's content.
     */
    SHAMapAddNode
    addKnownNode(
        SHAMapNodeID const& nodeID,
        SHAMapTreeNodePtr treeNode,
        SHAMapSyncFilter const* filter);

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
     * Only a leaf may sit at kLeafDepth, since an inner node there would have
     * no branch left to select. Both of the places that bound a descent call
     * this, so a walk with a caller-supplied path and one without cannot drift
     * apart and refuse at different nodes.
     *
     * The depth is tested before the node's type so the virtual call runs only
     * where the bound can bite, which is the last level of a 65-level walk.
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
     * A root-down path of nodes through the map.
     *
     * The path itself names each node's position: entry `i` sits at depth
     * `i`, since a SHAMap does not merge a single-child node away (see the
     * class docstring above), so every nibble down to a leaf has an inner
     * node of its own. Nothing is therefore stored per entry but the node.
     * No consumer needs a whole SHAMapNodeID: every one of them wants a
     * depth, and reads the nibbles it cares about from the key it already
     * holds.
     *
     * Storing an ID alongside each node would add a second answer to "where
     * does this node sit", which could then disagree with the first.
     * Deriving it cannot.
     */
    class NodePathStack
    {
    public:
        /**
         * @return whether the path holds no node at all.
         */
        [[nodiscard]] bool
        empty() const
        {
            return path_.empty();
        }

        /**
         * @return how many nodes the path holds, which is one more than its
         *         last node's depth.
         */
        [[nodiscard]] std::size_t
        size() const
        {
            return path_.size();
        }

        /**
         * The node at the end of the path.
         *
         * Reading an empty path would be undefined, and the assert alone is
         * stripped in release, so an empty path yields a null node the caller
         * can test instead.
         *
         * @return a reference into the path, which a later push may
         *         invalidate by reallocating. Callers that push and then want
         *         the node again ask for it again; the node itself does not
         *         move, only the slot holding the pointer to it.
         */
        [[nodiscard]] SHAMapTreeNodePtr const&
        top() const
        {
            if (path_.empty())
            {
                // LCOV_EXCL_START
                UNREACHABLE("xrpl::SHAMap::NodePathStack::top : empty stack");
                static SHAMapTreeNodePtr const kEmpty;
                return kEmpty;
                // LCOV_EXCL_STOP
            }
            return path_.back();
        }

        /**
         * The depth of the node at the end of the path.
         *
         * @return the depth, which is the entry's own index; zero on an empty
         *         path, which a caller must not read but which must not be an
         *         out-of-range subtraction either.
         */
        [[nodiscard]] unsigned int
        topDepth() const
        {
            if (path_.empty())
            {
                // LCOV_EXCL_START
                UNREACHABLE("xrpl::SHAMap::NodePathStack::topDepth : empty stack");
                return 0;
                // LCOV_EXCL_STOP
            }
            return static_cast<unsigned int>(path_.size() - 1);
        }

        /**
         * Shorten the path by one node.
         *
         * Popping an empty path would be undefined, and the assert alone is
         * stripped in release, so an empty path is left alone instead.
         */
        void
        pop()
        {
            if (path_.empty())
            {
                // LCOV_EXCL_START
                UNREACHABLE("xrpl::SHAMap::NodePathStack::pop : empty stack");
                return;
                // LCOV_EXCL_STOP
            }
            path_.pop_back();
        }

        /**
         * Discard the whole path.
         *
         * For a walk that pushed a node it then found unusable: the node never
         * became a meaningful path entry, so it must not be mistaken for one
         * by whatever the caller does next with an empty-vs-nonempty check.
         */
        void
        clear()
        {
            path_.clear();
            pathKey_ = uint256{};
        }

        /**
         * Shorten the path by one node and hand that node to the caller.
         *
         * Reading a node out and then popping copies it, which costs an atomic
         * increment on its refcount. Moving it out does not.
         *
         * @return the node that was at the end of the path, or an empty
         *         pointer if there was none.
         */
        [[nodiscard]] SHAMapTreeNodePtr
        releaseNode()
        {
            if (path_.empty())
            {
                // LCOV_EXCL_START
                UNREACHABLE("xrpl::SHAMap::NodePathStack::releaseNode : empty stack");
                return {};
                // LCOV_EXCL_STOP
            }
            auto node = std::move(path_.back());
            path_.pop_back();
            return node;
        }

        /**
         * Start a path at the root of the map, which sits at depth zero by
         * definition.
         *
         * @return false, leaving the path unchanged, if a path was already
         *         started. A malformed call must not abort a release build,
         *         so callers stop rather than overwrite it.
         */
        [[nodiscard]] bool
        pushRoot(SHAMapTreeNodePtr node)
        {
            if (!path_.empty())
            {
                // LCOV_EXCL_START
                UNREACHABLE("xrpl::SHAMap::NodePathStack::pushRoot : non-empty stack");
                return false;
                // LCOV_EXCL_STOP
            }
            path_.push_back(std::move(node));
            return true;
        }

        /**
         * Extend the path to the child of the node at its end reached by
         * `branch`.
         *
         * The branch is not stored. It is only used to judge the node offered,
         * since the child's position is this path one level longer whichever
         * branch reached it.
         *
         * Only a leaf may sit at kLeafDepth, since an inner node there would
         * have no branch left to select.
         *
         * @param node the child to append.
         * @param branch the branch of the current node that `node` was
         *               reached through.
         * @return false if there is no node to descend from, no node to push,
         *         no branch of that number, no room left below for the kind
         *         of node offered, or a leaf whose own key does not select
         *         `branch`. A malformed call or a malformed map must not abort
         *         a release build, so callers stop walking instead. The path
         *         keeps its nodes, though the recorded branch chain may
         *         already name `branch`, which no later read reaches.
         */
        [[nodiscard]] bool
        pushChild(SHAMapTreeNodePtr node, unsigned int branch)
        {
            if (path_.empty() || !node || branch >= kBranchFactor)
            {
                // LCOV_EXCL_START
                UNREACHABLE("xrpl::SHAMap::NodePathStack::pushChild : no child to push");
                return false;
                // LCOV_EXCL_STOP
            }

            // Only a leaf may sit at kLeafDepth, so an inner child must land one level short of
            // it, tighter than the plain depth bound a leaf child needs.
            //
            // Reachable, for the same reason the misplaced-leaf case below is: a node resolved from
            // the local store has had neither its position nor its type judged. The two-argument
            // SHAMap::descend fetches by the parent's recorded child hash and hooks what comes
            // back, and a parsed node adopts that hash rather than recomputing it, so an inner node
            // can arrive one level too deep. So this refuses rather than aborting a build.
            //
            auto const parentDepth = topDepth();
            bool const tooDeep = pastLeafDepth(parentDepth, *node);
            SOMETIMES(tooDeep, "xrpl::SHAMap::NodePathStack::pushChild : child past leaf depth");
            if (tooDeep)
            {
                return false;
            }

            // Record the branch, then judge a leaf against every branch recorded so far. Testing
            // only this step's nibble would accept a whole subtree hung under the wrong branch:
            // one wrong child pointer in one inner node leaves every leaf below it agreeing at its
            // own final nibble, because the subtree is internally well formed, and disagreeing only
            // at the level where the pointer is wrong.
            //
            // This is the one thing a path cannot derive. Its length gives every depth, but whether
            // the caller descended the branches it says it did is only visible against a real key.
            //
            // Reachable for the same reason, and by a wider route: a node arriving through a sync
            // filter is judged by hash, and a hash says nothing about position. The paths that hook
            // a node reject a misplaced one first (see SHAMap::descend and gmnProcessNodes), but a
            // map read lazily from the local store never passes through them.
            setNibble(parentDepth, branch);

            bool const misplaced = node->isLeaf() &&
                !SHAMapNodeID::createID(parentDepth + 1u, pathKey_).isPrefixOf(leafKey(*node));
            SOMETIMES(
                misplaced, "xrpl::SHAMap::NodePathStack::pushChild : leaf key outside branch");
            if (misplaced)
            {
                return false;
            }

            path_.push_back(std::move(node));
            return true;
        }

        /**
         * Extend the path by one node lying on the way to `target`, starting
         * it if it is empty.
         *
         * For nodes not reached by descending a known branch: the walk tracks
         * only the key it is heading for, or the node is newly created. Either
         * way `target` names the branch.
         *
         * @param node the node to append.
         * @param target the key the walk is heading for.
         * @return whatever pushRoot or pushChild returned.
         */
        [[nodiscard]] bool
        pushNode(SHAMapTreeNodePtr node, uint256 const& target)
        {
            if (path_.empty())
            {
                return pushRoot(std::move(node));
            }
            return pushChild(std::move(node), selectBranch(topDepth(), target));
        }

    private:
        /**
         * Write `branch` as the nibble at `depth` of the recorded branch chain.
         *
         * @param depth the nibble index to write, which is the depth the
         *              branch was taken from.
         * @param branch the branch taken, which the caller has already bounded.
         */
        void
        setNibble(unsigned int depth, unsigned int branch)
        {
            auto& byte = *(pathKey_.begin() + (depth / 2));
            if ((depth & 1) != 0u)
            {
                byte = static_cast<unsigned char>((byte & 0xF0u) | branch);
            }
            else
            {
                byte = static_cast<unsigned char>((byte & 0x0Fu) | (branch << 4));
            }
        }

        // path_[i] holds the node at depth i, by construction: pushRoot starts at depth 0 and
        // pushChild only ever appends one level.
        std::vector<SHAMapTreeNodePtr> path_;

        // The branches descended, one nibble per level: nibble i is the branch taken from depth i.
        // One record for the whole path rather than an ID per entry, so path_.size() stays the only
        // answer to where a node sits and this is only the claim being checked against it.
        //
        // A pop leaves the nibbles above the path's end as they were, because no read can reach
        // them: createID masks this at parentDepth + 1, so a check reads nibbles 0 through
        // parentDepth only, and those are always the current path's. Level j + 1 exists only if a
        // push at depth j wrote nibble j, and re-descending at j overwrites it.
        uint256 pathKey_;
    };

    using DeltaRef =
        std::pair<boost::intrusive_ptr<SHAMapItem const>, boost::intrusive_ptr<SHAMapItem const>>;

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
    dirtyUp(NodePathStack& stack, uint256 const& target, SHAMapTreeNodePtr terminal);

    /**
     * Walk towards the specified id, returning the node.
     *
     * @param id the key to walk towards, which need not be in the map.
     * @param stack records the path walked, or nullptr to skip recording it.
     *              Lookups that only want the leaf (see findKey) omit it to
     *              avoid building a path they would immediately discard.
     * @return the leaf the walk ended on, or nullptr if it ended on an inner
     *         node or was refused. A returned leaf need not hold `id`, so
     *         callers compare its key themselves.
     */
    SHAMapLeafNode*
    walkTowardsKey(uint256 const& id, NodePathStack* stack = nullptr) const;
    /**
     * Return nullptr if key not found
     */
    SHAMapLeafNode*
    findKey(uint256 const& id) const;

    /**
     * Unshare the node, allowing it to be modified
     *
     * @param node the node to unshare.
     * @param depth the depth the node sits at, which says whether it is the
     *        root. A clone of the root has to be adopted as the new root; a
     *        clone of any other node is hooked up by the caller walking back
     *        up the path.
     * @return the node, cloned if it was shared.
     */
    template <class Node>
    intr_ptr::SharedPtr<Node>
    unshareNode(intr_ptr::SharedPtr<Node> node, unsigned int depth);

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
    SHAMapLeafNode*
    belowHelper(NodePathStack& stack, BelowDirection direction) const;

    /**
     * The nearest item on one side of `id`, which upperBound and lowerBound
     * both answer.
     *
     * @param id the key to search around, which need not be in the map.
     * @param direction First for the nearest key greater than `id`, Last for
     *                  the nearest lesser.
     * @return an iterator at that item, or end() if the map holds no key on
     *         that side.
     */
    [[nodiscard]] ConstIterator
    boundHelper(uint256 const& id, BelowDirection direction) const;

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
    using descendCallback = std::function<void(SHAMapTreeNodePtr, SHAMapHash const&)>;
    SHAMapTreeNode*
    descendAsync(
        SHAMapInnerNode* parent,
        unsigned int branch,
        SHAMapSyncFilter const* filter,
        bool& pending,
        descendCallback&&) const;

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
    hasLeafNode(uint256 const& tag, SHAMapHash const& hash) const;

    SHAMapLeafNode const*
    peekFirstItem(NodePathStack& stack) const;
    SHAMapLeafNode const*
    peekNextItem(uint256 const& id, NodePathStack& stack) const;
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
        std::vector<std::pair<SHAMapNodeID, uint256>> missingNodes;
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
     * Drains all of them even after judging the map, since an outstanding
     * read holds a pointer to `mn` and this is the only thing that waits for
     * it.
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
    XRPL_ASSERT(state_ != SHAMapState::Invalid, "xrpl::SHAMap::setImmutable : state is valid");
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
