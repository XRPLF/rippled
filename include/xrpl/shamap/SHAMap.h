/** @file
 *  Defines `SHAMap`, the authenticated radix-Merkle tree that underlies every
 *  XRP Ledger snapshot.
 *
 *  Every ledger carries two `SHAMap` instances: a transaction tree mapping
 *  transaction IDs to serialized transaction data, and a state tree mapping
 *  account-state object keys to their serialized values.  The root hash of
 *  each tree is what validators sign — two nodes hold the same ledger if and
 *  only if their root hashes match.
 *
 *  This header also defines `SHAMap::ConstIterator`, the forward iterator over
 *  leaf nodes in key order, and the `SHAMapState` enum that governs which
 *  operations are legal on a given map instance.
 */
#pragma once

#include <xrpl/basics/IntrusivePointer.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/shamap/Family.h>
#include <xrpl/shamap/SHAMapAddNode.h>
#include <xrpl/shamap/SHAMapInnerNode.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapLeafNode.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <set>
#include <stack>
#include <vector>

namespace xrpl {

class SHAMapNodeID;
class SHAMapSyncFilter;

/** Lifecycle state of a SHAMap instance.
 *
 *  Determines which mutation operations are legal.  The state may advance
 *  forward but never backward (except `Synching` → `Modifying` via
 *  `clearSynching()`).  Attempting to mutate a map in `Immutable` or
 *  `Invalid` state is guarded by asserts.
 */
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

/** Authenticated 16-way radix Merkle trie over 256-bit keys.
 *
 *  As a radix tree (fan-out 16), the key for a leaf is encoded entirely by
 *  its position in the tree (prefix property), and any inner node with only
 *  one descendant is collapsed upward (merge property).  Keys are consumed
 *  4 bits (one nibble) per level, giving a fixed leaf depth of 64.
 *
 *  As a Merkle tree, every inner node's hash is derived from the combined
 *  hashes of all 16 child slots (zero-hash for empty slots).  This allows
 *  O(log N) membership proofs and makes the root hash a cryptographic
 *  commitment to the entire ledger state.
 *
 *  **Copy-on-write (CoW)**: snapshots share physical nodes.  Each
 *  `SHAMapTreeNode` carries a `cowid_`; nodes owned exclusively by this map
 *  can be mutated in place.  Shared nodes are cloned on first write via
 *  `unshareNode()`.  `snapShot(isMutable=false)` on an immutable map is O(1)
 *  with zero node copies.
 *
 *  **Backing**: when `backed_ == true` (the default) the map integrates with
 *  the `Family`-provided `NodeStore` for persistent storage and cache lookups.
 *  Call `setUnbacked()` for transient in-memory trees (e.g., during
 *  transaction processing) that must not touch the database.
 *
 *  @note Not copyable or movable.  Use `snapShot()` to create a CoW
 *      derivative.
 *  @see SHAMapState, SHAMapItem, SHAMapInnerNode, SHAMapLeafNode, Family
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

    intr_ptr::SharedPtr<SHAMapTreeNode> root_;
    mutable SHAMapState state_;
    SHAMapType const type_;
    bool backed_ = true;         // Map is backed by the database
    mutable bool full_ = false;  // Map is believed complete in database

public:
    /** Number of children each inner node may have (the radix fan-out). */
    static constexpr unsigned int kBRANCH_FACTOR = SHAMapInnerNode::kBRANCH_FACTOR;

    /** Tree depth at which leaves reside; keys are 256 bits consumed 4 bits
     *  per level, so leaves are always at depth 64.
     */
    static constexpr unsigned int kLEAF_DEPTH = 64;

    /** A (before, after) pair of `SHAMapItem` pointers representing one changed
     *  entry in a delta computation.  A null `first` means the item was added;
     *  a null `second` means it was deleted; both non-null means it was
     *  modified.
     */
    using DeltaItem =
        std::pair<boost::intrusive_ptr<SHAMapItem const>, boost::intrusive_ptr<SHAMapItem const>>;

    /** Map from item key to its before/after state, produced by `compare()`. */
    using Delta = std::map<uint256, DeltaItem>;

    SHAMap() = delete;
    SHAMap(SHAMap const&) = delete;
    SHAMap&
    operator=(SHAMap const&) = delete;

    /** Construct a CoW snapshot of `other`.
     *
     *  No tree nodes are copied.  The new map shares `root_` with `other` and
     *  takes ownership of subsequent mutations through CoW cloning.  If either
     *  map is mutable, `unshare()` is called immediately so that later writes
     *  on one map cannot corrupt the other.  An immutable snapshot of an
     *  immutable map is O(1) with zero copies.
     *
     *  Prefer `snapShot()` over calling this constructor directly.
     *
     *  @param other      The source map to snapshot.
     *  @param isMutable  If `true`, the new map is in `Modifying` state;
     *      otherwise `Immutable`.
     */
    SHAMap(SHAMap const& other, bool isMutable);

    /** Construct a new, empty map in `Modifying` state.
     *
     *  @param t  Whether this map holds transactions (`SHAMapType::Transaction`)
     *      or account state (`SHAMapType::State`).
     *  @param f  The `Family` providing the NodeStore, caches, and journal.
     */
    SHAMap(SHAMapType t, Family& f);

    /** Construct a map in `Synching` state for a ledger whose root hash is
     *  known but whose nodes must still be fetched from peers.
     *
     *  The `hash` parameter is not stored internally; it serves as a
     *  documentation signal that callers should subsequently call
     *  `fetchRoot(hash, filter)` to install the root node.
     *
     *  @param t     Whether this map holds transactions or account state.
     *  @param hash  The expected root hash (not stored; used to select this
     *      overload over the two-argument form).
     *  @param f     The `Family` providing the NodeStore, caches, and journal.
     */
    SHAMap(SHAMapType t, uint256 const& hash, Family& f);

    ~SHAMap() = default;

    /** Return the `Family` that backs this map's storage and caches. */
    Family const&
    family() const
    {
        return f_;
    }

    /** Return the `Family` that backs this map's storage and caches. */
    Family&
    family()
    {
        return f_;
    }

    //--------------------------------------------------------------------------

    /** Forward iterator over leaf nodes in ascending key order.
     *
     *  Always const — see `ConstIterator` for details.  Satisfies
     *  `std::forward_iterator`.
     */
    class ConstIterator;

    /** Return an iterator to the first leaf in key order, or `end()` if empty. */
    ConstIterator
    begin() const;

    /** Return the past-the-end sentinel iterator. */
    ConstIterator
    end() const;

    //--------------------------------------------------------------------------

    /** Return a heap-allocated CoW snapshot of this map.
     *
     *  Delegates to the copy constructor.  If `isMutable` is false and this
     *  map is also immutable, no nodes are copied (O(1)).  If either side is
     *  mutable, `unshare()` is called to prevent cross-map corruption on
     *  subsequent writes.
     *
     *  @param isMutable  If `true`, the snapshot enters `Modifying` state and
     *      may be mutated independently.
     *  @return A `shared_ptr` to the new snapshot.
     */
    std::shared_ptr<SHAMap>
    snapShot(bool isMutable) const;

    /** Mark this map as expected to be locally complete.
     *
     *  Sets `full_ = true`, indicating the server wants all referenced nodes
     *  in durable storage.  A subsequent cache miss clears the flag and
     *  triggers missing-node acquisition via the `Family`.
     */
    void
    setFull();

    /** Associate this map with a specific ledger sequence number.
     *
     *  The sequence number is passed to `NodeStore` fetch calls and to the
     *  missing-node acquisition callback so that the inbound-ledger pipeline
     *  can identify which ledger needs repair.
     *
     *  @param lseq  The ledger sequence number.
     */
    void
    setLedgerSeq(std::uint32_t lseq);

    /** Fetch and install the root node for a map in `Synching` state.
     *
     *  Attempts to locate the node identified by `hash` via the tiered fetch
     *  path (cache → NodeStore → `filter`).  On success, `root_` is replaced
     *  and the map is ready for further node ingestion via `addKnownNode()`.
     *
     *  @param hash    Expected hash of the root node.
     *  @param filter  Optional sync filter consulted if the node is not in the
     *      cache or NodeStore; may be `nullptr`.
     *  @return `true` if the root was successfully installed.
     */
    bool
    fetchRoot(SHAMapHash const& hash, SHAMapSyncFilter* filter);

    /** Return `true` if the map contains an item whose key equals `id`. */
    bool
    hasItem(uint256 const& id) const;

    /** Remove the item with key `id` from the map.
     *
     *  If the deletion leaves a parent inner node with only one child, that
     *  node is collapsed upward (merge property).  The map must be in
     *  `Modifying` state.
     *
     *  @param id  Key of the item to remove.
     *  @return `true` if the item was found and removed; `false` if not found.
     */
    bool
    delItem(uint256 const& id);

    /** Insert a new item into the map, taking shared ownership of `item`.
     *
     *  Forwards to `addGiveItem()`.  The map must be in `Modifying` state and
     *  must not already contain an item with the same key.
     *
     *  @param type  Leaf type determining the hash prefix and wire format.
     *  @param item  The item to insert.
     *  @return `true` if the item was inserted; `false` if a duplicate key
     *      already exists.
     */
    bool
    addItem(SHAMapNodeType type, boost::intrusive_ptr<SHAMapItem const> item);

    /** Compute and return the root hash of this map.
     *
     *  If any node hashes are dirty the tree is re-hashed bottom-up before
     *  returning.  This involves a `const_cast` internally (logical const,
     *  physical mutate) — acknowledged design compromise documented in
     *  `SHAMap.cpp`.
     *
     *  @return The `SHAMapHash` of the root node.
     */
    SHAMapHash
    getHash() const;

    /** Replace an existing item in the map, transferring ownership of `item`.
     *
     *  Locates the leaf whose key matches `item->key()` and replaces its
     *  payload.  Calls `dirtyUp()` only when `setItem()` reports the hash
     *  changed, avoiding spurious rehashing for no-op updates.  The map must
     *  be in `Modifying` state.
     *
     *  @param type  Leaf type (must match the type the item was inserted with).
     *  @param item  Replacement payload; its `key()` must match an existing
     *      entry.
     *  @return `true` if the item was found and updated; `false` if not found.
     */
    bool
    updateGiveItem(SHAMapNodeType type, boost::intrusive_ptr<SHAMapItem const> item);

    /** Insert a new item into the map, transferring ownership of `item`.
     *
     *  If the target leaf position is empty, a new leaf is created there.  If
     *  it collides with an existing leaf whose key differs, inner nodes are
     *  created at deeper levels until the two keys' nibbles diverge (respecting
     *  the merge property).  The map must be in `Modifying` state.
     *
     *  @param type  Leaf type determining the hash prefix and wire format.
     *  @param item  The item to insert; its `key()` must not already exist.
     *  @return `true` if the item was inserted; `false` if a duplicate key
     *      already exists.
     */
    bool
    addGiveItem(SHAMapNodeType type, boost::intrusive_ptr<SHAMapItem const> item);

    /** Return a reference to the intrusive pointer for the item with key `id`.
     *
     *  The reference is valid only while the map is not mutated.  To keep the
     *  item alive beyond the map's next mutation, copy the intrusive pointer.
     *
     *  @param id  Key to look up.
     *  @return Reference to the stored `intrusive_ptr`, or to a null pointer
     *      if `id` is not found.
     */
    boost::intrusive_ptr<SHAMapItem const> const&
    peekItem(uint256 const& id) const;

    /** Return a reference to the intrusive pointer for `id`, also supplying
     *  the leaf's hash.
     *
     *  @param id    Key to look up.
     *  @param hash  Receives the hash of the leaf node on success; unchanged
     *      on miss.
     *  @return Reference to the stored `intrusive_ptr`, or to a null pointer
     *      if `id` is not found.
     */
    boost::intrusive_ptr<SHAMapItem const> const&
    peekItem(uint256 const& id, SHAMapHash& hash) const;

    /** Return an iterator to the first leaf whose key is strictly greater than
     *  `id`.
     *
     *  `id` does not need to exist in the map.
     *
     *  @param id  Lower-bound key (exclusive).
     *  @return Iterator to the first leaf with key > `id`, or `end()`.
     */
    ConstIterator
    upperBound(uint256 const& id) const;

    /** Return an iterator to the first leaf whose key is greater than or equal
     *  to `id`.
     *
     *  `id` does not need to exist in the map.
     *
     *  @param id  Lower-bound key (inclusive).
     *  @return Iterator to the first leaf with key >= `id`, or `end()`.
     */
    ConstIterator
    lowerBound(uint256 const& id) const;

    /** Invoke `function` on every node (inner and leaf) in the tree.
     *
     *  Uses an explicit stack to avoid recursion on 64-level trees.
     *  Traversal order is unspecified but consistent within a single call.
     *
     *  @param function  Callable invoked with each `SHAMapTreeNode&`.  Return
     *      `false` to stop traversal early.
     */
    void
    visitNodes(std::function<bool(SHAMapTreeNode&)> const& function) const;

    /** Invoke `function` on every node present in this map but absent from
     *  `have`.
     *
     *  Short-circuits at hash equality, so it is O(d) in the number of
     *  differing nodes rather than O(n) in total nodes.
     *
     *  @param have      The map whose nodes are considered "already present";
     *      may be `nullptr` (treats every node in `this` as absent from `have`).
     *  @param function  Callable invoked with each differing `SHAMapTreeNode
     *      const&`.  Return `false` to stop traversal early.
     */
    void
    visitDifferences(SHAMap const* have, std::function<bool(SHAMapTreeNode const&)> const&) const;

    /** Invoke `function` on the payload of every leaf node in the tree.
     *
     *  Delegates to `visitNodes`, filtering out inner nodes.
     *
     *  @param function  Callable invoked with each leaf's
     *      `boost::intrusive_ptr<SHAMapItem const>`.
     */
    void
    visitLeaves(std::function<void(boost::intrusive_ptr<SHAMapItem const> const&)> const&) const;

    /** Traverse the tree to discover nodes that are referenced but not
     *  available locally, maximizing I/O concurrency via async fetches.
     *
     *  Uses the `MissingNodes` DFS engine with bounded async concurrency
     *  (`maxDefer = 512`).  Subtrees confirmed complete via the
     *  `FullBelowCache` are skipped.  Random start nibbles per inner node
     *  spread concurrent callers across different parts of the tree.
     *
     *  @param maxNodes  Stop after collecting this many missing-node entries.
     *  @param filter    Optional sync filter consulted for each missing hash;
     *      may be `nullptr`.
     *  @return Vector of `(SHAMapNodeID, hash)` pairs for nodes that could not
     *      be located.
     */
    std::vector<std::pair<SHAMapNodeID, uint256>>
    getMissingNodes(int maxNodes, SHAMapSyncFilter* filter);

    /** Serialize a node and a bounded-depth subtree for peer delivery.
     *
     *  Bundles the target node with adjacent inner nodes up to `depth` levels
     *  deep.  The depth budget is decremented only when an inner node has more
     *  than one non-empty child; single-child chains (compressed radix paths)
     *  are traversed for free.  This amortizes per-message round-trip cost
     *  during tree synchronization.
     *
     *  @param wanted    The `SHAMapNodeID` of the node the peer requested.
     *  @param data      Output vector; each appended entry is a
     *      `(SHAMapNodeID, wire-serialized blob)` pair.
     *  @param fatLeaves If `true`, leaf nodes adjacent to traversed inner nodes
     *      are also included; otherwise only inner nodes are emitted.
     *  @param depth     Maximum number of additional levels to bundle.
     *  @return `true` if the requested node was found and at least one entry
     *      was appended; `false` if the node could not be located.
     */
    bool
    getNodeFat(
        SHAMapNodeID const& wanted,
        std::vector<std::pair<SHAMapNodeID, Blob>>& data,
        bool fatLeaves,
        std::uint32_t depth) const;

    /** Collect the Merkle proof path for `key`.
     *
     *  Walks from the root toward `key`, collecting the serialized form of
     *  every node on the path (leaf first, root last).  Sibling hashes are
     *  encoded inside each parent's serialization, enabling the recipient to
     *  reconstruct and verify the root hash without the full tree.
     *
     *  @param key  256-bit key of the target leaf.
     *  @return A vector of serialized node blobs from leaf to root if `key`
     *      is present in the map; `std::nullopt` if not found.
     */
    std::optional<std::vector<Blob>>
    getProofPath(uint256 const& key) const;

    /** Verify a Merkle proof path without a live tree.
     *
     *  Recomputes the root hash from `path` (leaf-to-root order) using the
     *  nibbles of `key` to select the correct branch at each level, and
     *  compares the result against `rootHash`.  Bounded to 65 levels; network
     *  input is wrapped in try/catch to handle malformed blobs.
     *
     *  @param rootHash  Expected root hash of the tree.
     *  @param key       256-bit key that the path was generated for.  The
     *      verifier uses this key's nibbles to navigate — a wrong key at any
     *      level produces a false negative.
     *  @param path      Serialized nodes from leaf to root, as returned by
     *      `getProofPath()`.
     *  @return `true` if the recomputed root matches `rootHash`.
     */
    static bool
    verifyProofPath(uint256 const& rootHash, uint256 const& key, std::vector<Blob> const& path);

    /** Serialize the root node into `s` in the wire format used for peer
     *  messages.
     *
     *  @param s  Serializer to append the encoded root node to.
     */
    void
    serializeRoot(Serializer& s) const;

    /** Install the root node received from a peer during synchronization.
     *
     *  Deserializes `rootNode`, verifies its hash matches `hash`, and
     *  installs it as `root_` if the map is in `Synching` state and the
     *  current root is empty.  Notifies `filter` on success so it can persist
     *  or cache the data.
     *
     *  @param hash      Expected hash of the root node.
     *  @param rootNode  Wire-serialized root node received from a peer.
     *  @param filter    Optional sync filter for notification; may be `nullptr`.
     *  @return A `SHAMapAddNode` tri-state: useful (new root installed),
     *      duplicate (root already present), or invalid (hash mismatch or
     *      deserialization failure).
     */
    SHAMapAddNode
    addRootNode(SHAMapHash const& hash, Slice const& rootNode, SHAMapSyncFilter* filter);

    /** Install a known interior or leaf node received from a peer.
     *
     *  Performs two integrity checks before installing: (1) the deserialized
     *  node's hash must match the hash recorded in its parent branch; (2) for
     *  leaves at `kLEAF_DEPTH`, the reconstructed `SHAMapNodeID` from the
     *  leaf's own key must match `nodeID`, closing a theoretical
     *  hash-collision-at-wrong-position attack.  A mismatch on either check
     *  transitions the map to `Invalid` rather than crashing.
     *
     *  Skips descent into subtrees already confirmed complete via the
     *  `FullBelowCache`.
     *
     *  @param nodeID   Tree address of the node being provided.
     *  @param rawNode  Wire-serialized node data.
     *  @param filter   Optional sync filter; may be `nullptr`.
     *  @return `SHAMapAddNode` tri-state: useful, duplicate, or invalid.
     *  @note Returning `invalid` is normal for stale or mismatched peer data;
     *      callers must handle it gracefully without crashing.
     */
    SHAMapAddNode
    addKnownNode(SHAMapNodeID const& nodeID, Slice const& rawNode, SHAMapSyncFilter* filter);

    /** Freeze the map, forbidding all future writes.
     *
     *  Advances the state from `Modifying` or `Synching` to `Immutable`.
     *  Asserts that the current state is not `Invalid` — only coherent maps
     *  may be frozen.  After this call, any attempt to mutate the map will
     *  trigger an assertion failure.
     */
    void
    setImmutable();

    /** Return `true` if the map is in `Synching` state. */
    bool
    isSynching() const;

    /** Advance the map to `Synching` state to begin peer synchronization. */
    void
    setSynching();

    /** Revert the map from `Synching` back to `Modifying` state.
     *
     *  Used when synchronization is abandoned before completion so the map
     *  can be used for local modifications again.
     */
    void
    clearSynching();

    /** Return `true` if the map is in any state other than `Invalid`. */
    bool
    isValid() const;

    /** Compute the symmetric difference between this map and `otherMap`.
     *
     *  Walks both trees concurrently, short-circuiting at subtree roots whose
     *  hashes match (O(d) in the number of differences, not O(n) in total
     *  items).  Results are recorded in `differences` as `DeltaItem` pairs.
     *
     *  @param otherMap     The map to compare against.  Must not be accessed
     *      concurrently by any other caller for the duration of this call.
     *  @param differences  Receives one entry per key that differs between the
     *      two maps.  Entries are appended; the caller is responsible for
     *      starting with an empty map if that is desired.
     *  @param maxCount     Maximum number of differences to record before
     *      returning early.  Pass `INT_MAX` for unlimited.
     *  @return `true` if the comparison completed without hitting `maxCount`;
     *      `false` if truncated.
     */
    bool
    compare(SHAMap const& otherMap, Delta& differences, int maxCount) const;

    /** Set `cowid_` to zero on every node, making them all shareable.
     *
     *  After this call the map's nodes may be safely shared across CoW
     *  snapshots without cloning.  Does not write to the NodeStore.
     *
     *  @return The number of nodes processed.
     */
    int
    unshare();

    /** Serialize and persist dirty nodes, then mark them as shared.
     *
     *  Performs a post-order DFS with an explicit stack to avoid stack
     *  overflow on 64-deep trees.  Per node: clones if CoW-shared, recomputes
     *  hash, clears `cowid_`, and writes to `Family::db()` if `backed_` is
     *  `true`.
     *
     *  @param t  The `NodeObjectType` tag used when writing to the NodeStore.
     *  @return The number of nodes written.
     */
    int
    flushDirty(NodeObjectType t);

    /** Single-threaded completeness check; records missing nodes up to a limit.
     *
     *  Traverses the tree, attempting to fetch each referenced node via
     *  `descendNoStore` (returns null instead of throwing on a miss).  Any
     *  unreachable node is appended to `missingNodes`.
     *
     *  @param missingNodes  Output vector; missing nodes are appended.
     *  @param maxMissing    Stop after recording this many missing nodes.
     */
    void
    walkMap(std::vector<SHAMapMissingNode>& missingNodes, int maxMissing) const;

    /** Parallel completeness check using one thread per top-level branch.
     *
     *  Partitions at depth 1 — up to 16 `std::thread`s, one per non-empty,
     *  non-leaf top-level child.  Each thread has its own node stack;
     *  `missingNodes` and an internal exceptions vector are mutex-protected.
     *
     *  @param missingNodes  Output vector; missing nodes are appended (all
     *      threads share it under a mutex).
     *  @param maxMissing    Stop after recording this many missing nodes.
     *  @return `true` if no worker thread threw an exception; `false`
     *      otherwise.  A `false` return does NOT mean there are no missing
     *      nodes — always inspect `missingNodes` regardless of the return
     *      value.
     *  @note Worker threads catch `SHAMapMissingNode` to avoid `std::terminate`;
     *      uncaught exceptions are stored internally and reflected only in
     *      the return value.  Callers must inspect both the return value and
     *      `missingNodes` for a complete picture.
     */
    bool
    walkMapParallel(std::vector<SHAMapMissingNode>& missingNodes, int maxMissing) const;

    /** Item-by-item equality check for debug and test use only.
     *
     *  Slower than hash comparison; intended for validating test fixtures.
     *
     *  @param other  The map to compare against.
     *  @return `true` if both maps contain identical items.
     */
    bool
    deepCompare(SHAMap& other) const;

    /** Detach this map from the NodeStore, making all I/O in-memory only.
     *
     *  Sets `backed_ = false`.  Subsequent fetches consult only the in-process
     *  `TreeNodeCache`; all writes are suppressed.  Use for transient trees
     *  (e.g., during transaction processing) that must not touch the database.
     */
    void
    setUnbacked();

    /** Log every node in the tree to the journal for debugging. */
    void
    dump(bool withHashes = false) const;

    /** Assert that all structural invariants hold across the entire tree.
     *
     *  Checks that every node satisfies its own `invariants()`, that inner
     *  nodes' `isBranch_` bitmask is consistent with their children, and that
     *  no leaf appears above `kLEAF_DEPTH`.  Intended for test and debug
     *  builds; a violation triggers an assertion failure.
     */
    void
    invariants() const;

private:
    using SharedPtrNodeStack =
        std::stack<std::pair<intr_ptr::SharedPtr<SHAMapTreeNode>, SHAMapNodeID>>;
    using DeltaRef =
        std::pair<boost::intrusive_ptr<SHAMapItem const>, boost::intrusive_ptr<SHAMapItem const>>;

    // tree node cache operations
    intr_ptr::SharedPtr<SHAMapTreeNode>
    cacheLookup(SHAMapHash const& hash) const;

    void
    canonicalize(SHAMapHash const& hash, intr_ptr::SharedPtr<SHAMapTreeNode>&) const;

    // database operations
    intr_ptr::SharedPtr<SHAMapTreeNode>
    fetchNodeFromDB(SHAMapHash const& hash) const;
    intr_ptr::SharedPtr<SHAMapTreeNode>
    fetchNodeNT(SHAMapHash const& hash) const;
    intr_ptr::SharedPtr<SHAMapTreeNode>
    fetchNodeNT(SHAMapHash const& hash, SHAMapSyncFilter* filter) const;
    intr_ptr::SharedPtr<SHAMapTreeNode>
    fetchNode(SHAMapHash const& hash) const;
    intr_ptr::SharedPtr<SHAMapTreeNode>
    checkFilter(SHAMapHash const& hash, SHAMapSyncFilter* filter) const;

    /** Update hashes up to the root */
    void
    dirtyUp(
        SharedPtrNodeStack& stack,
        uint256 const& target,
        intr_ptr::SharedPtr<SHAMapTreeNode> terminal);

    /** Walk towards the specified id, returning the node.  Caller must check
        if the return is nullptr, and if not, if the node->peekItem()->key() ==
       id */
    SHAMapLeafNode*
    walkTowardsKey(uint256 const& id, SharedPtrNodeStack* stack = nullptr) const;
    /** Return nullptr if key not found */
    SHAMapLeafNode*
    findKey(uint256 const& id) const;

    /** Unshare the node, allowing it to be modified */
    template <class Node>
    intr_ptr::SharedPtr<Node>
    unshareNode(intr_ptr::SharedPtr<Node>, SHAMapNodeID const& nodeID);

    /** prepare a node to be modified before flushing */
    template <class Node>
    intr_ptr::SharedPtr<Node>
    preFlushNode(intr_ptr::SharedPtr<Node> node) const;

    /** write and canonicalize modified node */
    intr_ptr::SharedPtr<SHAMapTreeNode>
    writeNode(NodeObjectType t, intr_ptr::SharedPtr<SHAMapTreeNode> node) const;

    // returns the first item at or below this node
    SHAMapLeafNode*
    firstBelow(intr_ptr::SharedPtr<SHAMapTreeNode>, SharedPtrNodeStack& stack, int branch = 0)
        const;

    // returns the last item at or below this node
    SHAMapLeafNode*
    lastBelow(
        intr_ptr::SharedPtr<SHAMapTreeNode> node,
        SharedPtrNodeStack& stack,
        int branch = kBRANCH_FACTOR) const;

    // helper function for firstBelow and lastBelow
    SHAMapLeafNode*
    belowHelper(
        intr_ptr::SharedPtr<SHAMapTreeNode> node,
        SharedPtrNodeStack& stack,
        int branch,
        std::tuple<int, std::function<bool(int)>, std::function<void(int&)>> const& loopParams)
        const;

    // Simple descent
    // Get a child of the specified node
    SHAMapTreeNode*
    descend(SHAMapInnerNode*, int branch) const;
    SHAMapTreeNode*
    descendThrow(SHAMapInnerNode*, int branch) const;
    intr_ptr::SharedPtr<SHAMapTreeNode>
    descend(SHAMapInnerNode&, int branch) const;
    intr_ptr::SharedPtr<SHAMapTreeNode>
    descendThrow(SHAMapInnerNode&, int branch) const;

    // Descend with filter
    // If pending, callback is called as if it called fetchNodeNT
    using descendCallback =
        std::function<void(intr_ptr::SharedPtr<SHAMapTreeNode>, SHAMapHash const&)>;
    SHAMapTreeNode*
    descendAsync(
        SHAMapInnerNode* parent,
        int branch,
        SHAMapSyncFilter* filter,
        bool& pending,
        descendCallback&&) const;

    std::pair<SHAMapTreeNode*, SHAMapNodeID>
    descend(
        SHAMapInnerNode* parent,
        SHAMapNodeID const& parentID,
        int branch,
        SHAMapSyncFilter* filter) const;

    // Non-storing
    // Does not hook the returned node to its parent
    intr_ptr::SharedPtr<SHAMapTreeNode>
    descendNoStore(SHAMapInnerNode&, int branch) const;

    /** If there is only one leaf below this node, get its contents */
    boost::intrusive_ptr<SHAMapItem const> const&
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
        boost::intrusive_ptr<SHAMapItem const> const& otherMapItem,
        bool isFirstMap,
        Delta& differences,
        int& maxCount) const;
    int
    walkSubTree(bool doWrite, NodeObjectType t);

    /** Mutable traversal state for a single call to `getMissingNodes()`.
     *
     *  Bundles the DFS stack, async I/O bookkeeping, and result accumulation
     *  for one invocation of the missing-node discovery algorithm.  Non-
     *  copyable; created on the stack inside `getMissingNodes()` and passed by
     *  reference to the two helper functions.
     */
    struct MissingNodes
    {
        MissingNodes() = delete;
        MissingNodes(MissingNodes const&) = delete;
        MissingNodes&
        operator=(MissingNodes const&) = delete;

        /** Maximum number of missing nodes to collect before returning. */
        int max;

        /** Optional sync filter consulted for each missing hash. */
        SHAMapSyncFilter* filter;

        /** Maximum number of async reads to keep in flight simultaneously. */
        int const maxDefer;

        /** `FullBelowCache` generation at traversal start; subtrees whose
         *  `fullBelowGen_` matches this value are skipped as already complete.
         */
        std::uint32_t generation;

        /** Accumulated (SHAMapNodeID, hash) pairs for nodes confirmed absent. */
        std::vector<std::pair<SHAMapNodeID, uint256>> missingNodes;

        /** Hashes of missing nodes, for deduplication. */
        std::set<SHAMapHash> missingHashes;

        /** One entry per inner node actively being traversed. */
        using StackEntry = std::tuple<
            SHAMapInnerNode*,  // pointer to the node
            SHAMapNodeID,      // the node's ID
            int,               // which child we check first
            int,               // which child we check next
            bool>;             // whether we've found any missing children yet

        // We explicitly choose to specify the use of std::deque here, because
        // we need to ensure that pointers and/or references to existing
        // elements will not be invalidated during the course of element
        // insertion and removal. Containers that do not offer this guarantee,
        // such as std::vector, can't be used here.
        std::stack<StackEntry, std::deque<StackEntry>> stack;

        /** One completed async read awaiting processing. */
        using DeferredNode = std::tuple<
            SHAMapInnerNode*,                      // parent node
            SHAMapNodeID,                          // parent node ID
            int,                                   // branch
            intr_ptr::SharedPtr<SHAMapTreeNode>>;  // fetched node

        /** Count of async reads currently in flight. */
        int deferred;

        /** Guards `finishedReads` and `deferred` for async callbacks. */
        std::mutex deferLock;

        /** Signalled by async callbacks when `finishedReads` is non-empty. */
        std::condition_variable deferCondVar;

        /** Completed async reads waiting to be installed into the tree. */
        std::vector<DeferredNode> finishedReads;

        /** Inner nodes that must be revisited once their deferred children
         *  have been installed.
         */
        std::map<SHAMapInnerNode*, SHAMapNodeID> resumes;

        MissingNodes(int max, SHAMapSyncFilter* filter, int maxDefer, std::uint32_t generation)
            : max(max), filter(filter), maxDefer(maxDefer), generation(generation), deferred(0)
        {
            missingNodes.reserve(max);
            finishedReads.reserve(maxDefer);
        }
    };

    // getMissingNodes helper functions
    void
    gmnProcessNodes(MissingNodes&, MissingNodes::StackEntry& node);
    static void
    gmnProcessDeferredReads(MissingNodes&);

    // fetch from DB helper function
    intr_ptr::SharedPtr<SHAMapTreeNode>
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

/** Forward iterator over `SHAMap` leaf nodes in ascending key order.
 *
 *  Carries its own `SharedPtrNodeStack` — a path from the root to the current
 *  position — so that `operator++` can resume descent without rescanning from
 *  the root.  Always const: the Merkle invariant requires that any write
 *  re-hashes the entire path to the root, so a non-const iterator would either
 *  silently break the invariant or force expensive re-hashing on every
 *  dereference.
 *
 *  Satisfies `std::forward_iterator`.  Default-constructing is not permitted;
 *  obtain iterators from `SHAMap::begin()` and `SHAMap::end()`.
 *
 *  @note Comparing iterators from different `SHAMap` instances is undefined
 *      and will trigger an assertion in debug builds.
 */
class SHAMap::ConstIterator
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
    ConstIterator() = delete;

    ConstIterator(ConstIterator const& other) = default;
    ConstIterator&
    operator=(ConstIterator const& other) = default;

    ~ConstIterator() = default;

    /** Dereference the iterator, returning the current `SHAMapItem`. */
    reference
    operator*() const;

    /** Dereference the iterator, returning a pointer to the current item. */
    pointer
    operator->() const;

    /** Advance to the next leaf in key order and return a reference to this
     *  iterator.
     */
    ConstIterator&
    operator++();

    /** Advance to the next leaf in key order and return the prior iterator
     *  value.
     */
    ConstIterator
    operator++(int);

private:
    explicit ConstIterator(SHAMap const* map);
    ConstIterator(SHAMap const* map, std::nullptr_t);
    ConstIterator(SHAMap const* map, pointer item, SharedPtrNodeStack&& stack);

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

inline SHAMap::ConstIterator::ConstIterator(
    SHAMap const* map,
    pointer item,
    SharedPtrNodeStack&& stack)
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

/** Return `true` if `x` and `y` refer to the same leaf (or both are end).
 *
 *  @note Asserts in debug builds that both iterators belong to the same map.
 */
inline bool
operator==(SHAMap::ConstIterator const& x, SHAMap::ConstIterator const& y)
{
    XRPL_ASSERT(
        x.map_ == y.map_,
        "xrpl::operator==(SHAMap::const_iterator, SHAMap::const_iterator) : "
        "inputs map do match");
    return x.item_ == y.item_;
}

/** Return `true` if `x` and `y` do not refer to the same leaf. */
inline bool
operator!=(SHAMap::ConstIterator const& x, SHAMap::ConstIterator const& y)
{
    return !(x == y);
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
