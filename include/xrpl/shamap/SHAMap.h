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
 * A SHAMap is both a trie with a fan-out of 16 and a Merkle tree.
 *
 * A trie keeps a key in the position of its nodes rather than in the nodes
 * themselves: the path from the root down to a node spells out the prefix
 * every key below it shares (the "prefix property"). A SHAMap spends one
 * nibble of the 256-bit key per level, so an inner node has at most 16
 * children and a leaf sits at depth 64 at the deepest. A leaf also carries
 * its own full key, which is what lets a reader check that it was reached
 * through the branches that key names.
 *
 * A radix tree also merges a single-child node with its child (the "merge
 * property"), making it a compressed trie. A SHAMap does not: an insert
 * creates an inner node at every nibble two keys share and merges none away,
 * and a delete collapses a chain that reduces to one leaf. Either way no edge
 * spans more than one nibble, so a path has one entry per level with no gaps,
 * and 65 entries at the most. Traversal relies on that, since it is what makes
 * a path's length name each node's depth.
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

    // ledgerSeq_, state_ and full_ are touched on the nodestore fetch path and on a
    // getMissingNodes() walk, neither of which may block, so pin them lock-free.
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(std::atomic<SHAMapState>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);

    /**
     * The sequence of the ledger that this map references, if any.
     *
     * Written when a map's ledger sequence is established (Ledger::setFull(),
     * InboundLedger) while a nodestore reader thread reads it. Relaxed either
     * way: it only serves as a lookup hint for a nodestore keyed by hash, so
     * it orders nothing else.
     */
    std::atomic<std::uint32_t> ledgerSeq_ = 0;

    SHAMapTreeNodePtr root_;

    /**
     * The map's state.
     *
     * A getMissingNodes() walk writes it, through setInvalid() and
     * clearSynching(), while whatever drives the acquisition reads it.
     * Nothing here requires the caller to hold a lock across the walk, and
     * the acquisition code does not, so this is atomic rather than guarded.
     *
     * Mutable because reaching the Invalid verdict is not a modification of
     * the map the caller asked for: descend() resolves a node, judges its
     * position and records the verdict, all while const.
     */
    mutable std::atomic<SHAMapState> state_;
    SHAMapType const type_;
    bool backed_ = true;  // Map is backed by the database

    /**
     * Map is believed complete in database.
     *
     * finishFetch() clears it on whichever nodestore reader thread completes a
     * read - several at once for the reads a getMissingNodes() walk posts.
     */
    mutable std::atomic<bool> full_ = false;

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

    /**
     * Take a snapshot of the given map.
     *
     * @param other The map to snapshot. An Invalid source yields an Invalid
     *        snapshot, since the two share the same node structure.
     * @param isMutable Whether the snapshot may be modified. Ignored when other
     *        is Invalid, since that state outranks both alternatives.
     */
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

    /**
     * Return a new map that is a snapshot of this one.
     *
     * Handles copy on write for mutable snapshots. An invalid map yields an
     * invalid snapshot, since the two share the same node structure.
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
     * @param id the identifier of the item, which need not exist in the map.
     * @return an iterator at the first item with a greater key, or end() if the
     *         map holds no greater key.
     * @throws SHAMapMissingNode if the map cannot be walked. That is not the
     *         same answer as end(), which claims no greater key exists.
     */
    ConstIterator
    upperBound(uint256 const& id) const;

    /**
     * Find the object with the greatest object id smaller than the input id.
     *
     * @param id the identifier of the item, which need not exist in the map.
     * @return an iterator at the last item with a smaller key, or end() if the
     *         map holds no smaller key.
     * @throws SHAMapMissingNode if the map cannot be walked. That is not the
     *         same answer as end(), which claims no smaller key exists.
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
     * Marks the map Invalid and abandons the traversal on meeting an inner
     * node at or beyond kLeafDepth, a shape no valid tree can have, so
     * callers must re-check isValid() before reading an empty result as
     * "nothing left to fetch".
     *
     * @param maxNodes The maximum number of found nodes to return
     * @param filter The filter to use when retrieving nodes
     * @return The nodes known to be missing, or empty if the map is Invalid
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
     * A node no valid tree could hold makes the map Invalid, which is
     * terminal: the root hash committed to an impossible shape, so no peer
     * can satisfy it. An acquisition reaching this verdict must give up
     * rather than retry; nothing may promote the map back to a valid state.
     *
     * @param nodeID The position in the tree where this node belongs.
     * @param treeNode A deserialized tree node to add.
     * @param filter Optional sync filter to track received nodes.
     * @return Status indicating whether the node was useful, duplicate, or invalid.
     *
     * @note This function expects the treeNode to be a valid, deserialized
     *       SHAMapTreeNode. The caller is responsible for deserialization
     *       before calling this function. The position is not the caller's
     *       to guarantee: a leaf whose key does not lie under nodeID leaves
     *       the map invalid and returns invalid(), and so does a node
     *       offered to a map an earlier descent already condemned.
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
     * Mark this map as syncing, fixing its hash while still allowing missing
     * nodes to be added.
     *
     * Left unchanged if the map is Invalid, which is terminal - though that
     * case is itself treated as unreachable (and asserts in a build with
     * assertions enabled), since nothing should call this on a map that has
     * already been judged.
     */
    void
    setSynching();

    /**
     * Mark this map as no longer syncing, so it can be modified again.
     *
     * Does nothing if the map is Invalid, which is terminal.
     */
    void
    clearSynching();

    /**
     * Whether the map can still be the map it claims to be.
     *
     * Not "complete" and not "self-consistent": a map that is merely missing
     * nodes is valid, and stays valid until something proves the tree it is
     * syncing against cannot exist.
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
     * The path itself names each node's position: entry `i` sits at depth `i`,
     * since a SHAMap does not merge a single-child node away (see the class
     * docstring above), so every nibble down to a leaf has an inner node of
     * its own. Only the node is stored per entry. A caller that needs a
     * position reads a depth and takes the nibbles it wants from its own key.
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
         *         started. A malformed call must not be treated as
         *         unreachable, so callers stop rather than overwrite it.
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
            // Reserved here rather than in the constructor, because SHAMap::end()
            // default-constructs one of these and has to stay allocation-free. A path holds at most
            // kLeafDepth + 1 entries, so this one reservation covers every push the path can take
            // and no later push reallocates. That is what lets a caller hold a reference into the
            // path across a push.
            path_.reserve(kLeafDepth + 1u);
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
         *         `branch`. A malformed call or a malformed map must not be
         *         treated as unreachable, so callers stop walking instead. The
         *         path keeps its nodes. A refusal that got as far as judging
         *         the leaf leaves `branch` recorded at this depth, which no
         *         read can observe, for the reason given on `pathKey_`.
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

            // Reachable, for the same reason the misplaced-leaf case below is: a node resolved from
            // the local store has had neither its position nor its type judged. The two-argument
            // SHAMap::descend fetches by the parent's recorded child hash and hooks what comes
            // back, and a parsed node adopts that hash rather than recomputing it, so an inner node
            // can arrive one level too deep. So this refuses rather than treating it as
            // unreachable.
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
     * Orders state_ alone. The tree's nodes are still mutated without
     * ordering guarantees, so this says nothing about whether the rest of
     * the map is safe to read concurrently.
     *
     * @return The state as of the call, which a concurrent walk may
     *         already have moved past.
     */
    [[nodiscard]] SHAMapState
    state() const;

    /**
     * Record that the map is provably not the one it claims to be.
     *
     * Private because only the map itself can prove that, from a node that
     * contradicts the hashes it is syncing against. Cannot fail, since
     * Invalid outranks every other state; see trySetState().
     *
     * Const because a read-only walk is one of the places that can reach this
     * verdict: descend() judges a node's position while resolving it, and
     * descend() is const.
     */
    void
    setInvalid() const;

    /**
     * Move the map to a new state, atomically.
     *
     * The only writer of state_ past construction, so the order between
     * the states lives in one place: Invalid outranks all of them and is
     * always stored, while every other transition is refused once the map
     * is Invalid, which is what makes that verdict terminal.
     *
     * Const so that setInvalid() can be const too. state_ is mutable, which is
     * what makes that legal. The non-const transitions stay non-const, so this
     * does not make a const map settleable.
     *
     * @param desired The state to move to.
     * @return false if the map is Invalid and the requested state is not,
     *         leaving it unchanged; true otherwise.
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
    [[nodiscard]] intr_ptr::SharedPtr<Node>
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
    // Invalid is stored outright: a walk reaching that verdict has to win against a thread
    // settling the map.
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

inline void
SHAMap::setSynching()
{
    // Guarded, so this is not a way out of Invalid, matching clearSynching().
    if (!trySetState(SHAMapState::Synching))
    {
        // Unreachable: this is only ever called on a freshly constructed map, so nothing can have
        // synced against it and reached a verdict yet.
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::SHAMap::setSynching : map is invalid");
        // LCOV_EXCL_STOP
    }
}

inline void
SHAMap::clearSynching()
{
    // Guarded, so an invalid map stays invalid rather than moving back to Modifying, which would
    // pass isValid(). A refusal here is the contract, not a broken invariant, so it is reported
    // rather than asserted.
    SOMETIMES(!isValid(), "xrpl::SHAMap::clearSynching : map is invalid");
    if (!trySetState(SHAMapState::Modifying))
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
    // Through trySetState() like every other transition, so nothing writes state_ behind its back.
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
