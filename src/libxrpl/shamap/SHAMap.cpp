/** @file
 *  Implements SHAMap: construction, mutation, traversal, copy-on-write
 *  snapshotting, lazy node fetching, and persistence flush.
 *
 *  Every XRP Ledger snapshot (account state or transaction set) is stored as a
 *  SHAMap — a 16-way radix trie whose inner nodes hash their children, forming
 *  a Merkle tree.  The root hash cryptographically commits to the full content,
 *  so two nodes agree on ledger state iff their root hashes match.
 *
 *  Key design invariants maintained here:
 *  - Copy-on-write via `cowid_`: nodes are cloned before mutation only when
 *    shared with another map generation.
 *  - Lazy fetch: nodes absent from the in-process cache are pulled from the
 *    NodeStore or a `SHAMapSyncFilter` on demand.
 *  - Merge property: inner nodes exist only when two or more items share a
 *    common key prefix; single-child inner nodes collapse on deletion.
 */
#include <xrpl/shamap/SHAMap.h>

#include <xrpl/basics/IntrusivePointer.h>    // IWYU pragma: keep
#include <xrpl/basics/IntrusivePointer.ipp>  // IWYU pragma: keep
#include <xrpl/basics/Log.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/TaggedCache.ipp>  // IWYU pragma: keep
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/Family.h>
#include <xrpl/shamap/SHAMapAccountStateLeafNode.h>
#include <xrpl/shamap/SHAMapInnerNode.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapLeafNode.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapSyncFilter.h>
#include <xrpl/shamap/SHAMapTreeNode.h>
#include <xrpl/shamap/SHAMapTxLeafNode.h>
#include <xrpl/shamap/SHAMapTxPlusMetaLeafNode.h>

#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <stack>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl {

/** Construct a concrete leaf node of the appropriate subtype.
 *
 *  Maps `SHAMapNodeType` to one of `SHAMapTxLeafNode`,
 *  `SHAMapTxPlusMetaLeafNode`, or `SHAMapAccountStateLeafNode`.  The three
 *  subtypes differ in hash prefix and whether the item key is included in the
 *  hash, so callers must always supply the correct type.
 *
 *  @param type  The logical node type; must not be `TnInner`.
 *  @param item  The slab-allocated payload to store in the leaf.
 *  @param owner The `cowid_` of the map that will own this leaf.
 *  @return A freshly constructed leaf with `cowid == owner`.
 *  @throws LogicError if `type` is not one of the three recognised leaf types.
 */
[[nodiscard]] intr_ptr::SharedPtr<SHAMapLeafNode>
makeTypedLeaf(SHAMapNodeType type, boost::intrusive_ptr<SHAMapItem const> item, std::uint32_t owner)
{
    if (type == SHAMapNodeType::TnTransactionNm)
        return intr_ptr::makeShared<SHAMapTxLeafNode>(std::move(item), owner);

    if (type == SHAMapNodeType::TnTransactionMd)
        return intr_ptr::makeShared<SHAMapTxPlusMetaLeafNode>(std::move(item), owner);

    if (type == SHAMapNodeType::TnAccountState)
        return intr_ptr::makeShared<SHAMapAccountStateLeafNode>(std::move(item), owner);

    logicError(
        "Attempt to create leaf node of unknown type " +
        std::to_string(static_cast<std::underlying_type_t<SHAMapNodeType>>(type)));
}

/** Construct a new, empty map in `Modifying` state.
 *
 *  @param t  Whether this map holds transactions or account state.
 *  @param f  The `Family` providing the NodeStore and caches.
 */
SHAMap::SHAMap(SHAMapType t, Family& f)
    : f_(f), journal_(f.journal()), state_(SHAMapState::Modifying), type_(t)
{
    root_ = intr_ptr::makeShared<SHAMapInnerNode>(cowid_);
}

/** Construct a map in `Synching` state for a ledger whose root hash is known.
 *
 *  The `hash` parameter is not stored or used internally — it exists solely to
 *  signal caller intent and select this overload over the two-argument form.
 *  Once the root node is fetched via `fetchRoot()`, the map's `root_->getHash()`
 *  will equal `hash`.
 *
 *  @param t     Whether this map holds transactions or account state.
 *  @param hash  The expected root hash (used as a documentation signal only).
 *  @param f     The `Family` providing the NodeStore and caches.
 */
SHAMap::SHAMap(SHAMapType t, uint256 const& hash, Family& f)
    : f_(f), journal_(f.journal()), state_(SHAMapState::Synching), type_(t)
{
    root_ = intr_ptr::makeShared<SHAMapInnerNode>(cowid_);
}

/** Copy constructor used by `snapShot()` to create a CoW snapshot.
 *
 *  Shares `root_` with `other` and increments `cowid_` so that any subsequent
 *  mutation on either map will clone nodes before modifying them.  If either
 *  map is mutable, `unshare()` is called immediately to break node sharing and
 *  prevent concurrent mutations from corrupting the other map's tree.  An
 *  immutable snapshot of an immutable map skips `unshare()` entirely, making
 *  the operation O(1) with zero node copies.
 *
 *  @param other      The source map to snapshot.
 *  @param isMutable  If true, the new map enters `Modifying` state; otherwise
 *      `Immutable`.
 */
SHAMap::SHAMap(SHAMap const& other, bool isMutable)
    : f_(other.f_)
    , journal_(other.f_.journal())
    , cowid_(other.cowid_ + 1)
    , ledgerSeq_(other.ledgerSeq_)
    , root_(other.root_)
    , state_(isMutable ? SHAMapState::Modifying : SHAMapState::Immutable)
    , type_(other.type_)
    , backed_(other.backed_)
{
    // If either map may change, they cannot share nodes
    if ((state_ != SHAMapState::Immutable) || (other.state_ != SHAMapState::Immutable))
    {
        unshare();
    }
}

/** Return a heap-allocated CoW snapshot of this map.
 *
 *  @param isMutable  If true, the snapshot is in `Modifying` state and may be
 *      mutated independently of this map.  If false, the snapshot is
 *      `Immutable` and shares all nodes with this map at zero copy cost
 *      (provided this map is also immutable).
 *  @return A `shared_ptr` to the new snapshot.
 */
std::shared_ptr<SHAMap>
SHAMap::snapShot(bool isMutable) const
{
    return std::make_shared<SHAMap>(*this, isMutable);
}

/** Propagate a structural change up to the root, updating CoW ownership.
 *
 *  Consumes `stack` bottom-up.  For each inner node on the path, the node is
 *  CoW-unshared (cloned if its `cowid` differs from the map's), then `child`
 *  is linked into the appropriate branch via `setChild`.  On return, a chain of
 *  freshly owned inner nodes runs from the modification point to `root_`.
 *
 *  @param stack   Path of inner nodes from root down to (but not including)
 *      `child`; consumed on return.
 *  @param target  Key of the item being inserted, deleted, or updated; used to
 *      determine which branch to follow at each level.
 *  @param child   The new subtree to attach (leaf or inner node); must already
 *      carry this map's `cowid_`.
 */
void
SHAMap::dirtyUp(
    SharedPtrNodeStack& stack,
    uint256 const& target,
    intr_ptr::SharedPtr<SHAMapTreeNode> child)
{
    XRPL_ASSERT(
        (state_ != SHAMapState::Synching) && (state_ != SHAMapState::Immutable),
        "xrpl::SHAMap::dirtyUp : valid state");
    XRPL_ASSERT(child && (child->cowid() == cowid_), "xrpl::SHAMap::dirtyUp : valid child input");

    while (!stack.empty())
    {
        auto node = intr_ptr::dynamicPointerCast<SHAMapInnerNode>(stack.top().first);
        SHAMapNodeID const nodeID = stack.top().second;
        stack.pop();
        XRPL_ASSERT(node, "xrpl::SHAMap::dirtyUp : non-null node");

        int const branch = selectBranch(nodeID, target);
        XRPL_ASSERT(branch >= 0, "xrpl::SHAMap::dirtyUp : valid branch");

        node = unshareNode(std::move(node), nodeID);
        node->setChild(branch, std::move(child));

        child = std::move(node);
    }
}

/** Descend towards the leaf position for `id`, optionally recording the path.
 *
 *  At each level the 4-bit nibble of `id` at that depth is used to select the
 *  next branch.  Descent stops when a leaf is reached or an empty branch is
 *  encountered.  The returned leaf may hold a different key than `id` — callers
 *  that need an exact match must compare `leaf->peekItem()->key() == id`.
 *
 *  @param id     The 256-bit key to navigate towards.
 *  @param stack  If non-null, each visited node (including the terminal leaf or
 *      the last inner node before an empty branch) is pushed here.  Must be
 *      empty on entry.
 *  @return Pointer to the leaf at or nearest to `id`, or `nullptr` if the
 *      branch leading to `id` is empty.
 *  @throws SHAMapMissingNode if a non-empty branch references a node that
 *      cannot be fetched.
 */
SHAMapLeafNode*
SHAMap::walkTowardsKey(uint256 const& id, SharedPtrNodeStack* stack) const
{
    XRPL_ASSERT(
        stack == nullptr || stack->empty(), "xrpl::SHAMap::walkTowardsKey : empty stack input");
    auto inNode = root_;
    SHAMapNodeID nodeID;

    while (inNode->isInner())
    {
        if (stack != nullptr)
            stack->emplace(inNode, nodeID);

        auto const inner = intr_ptr::staticPointerCast<SHAMapInnerNode>(inNode);
        auto const branch = selectBranch(nodeID, id);
        if (inner->isEmptyBranch(branch))
            return nullptr;

        inNode = descendThrow(*inner, branch);
        nodeID = nodeID.getChildNodeID(branch);
    }

    if (stack != nullptr)
        stack->emplace(inNode, nodeID);
    return safeDowncast<SHAMapLeafNode*>(inNode.get());
}

/** Find the leaf whose key is exactly `id`, or return `nullptr`.
 *
 *  Delegates to `walkTowardsKey()` and performs an exact key comparison on
 *  the result.  The radix trie can terminate at a leaf whose prefix matches
 *  `id` but whose stored key diverges; this function filters that case out.
 *
 *  @param id  Key to look up.
 *  @return Pointer to the matching leaf, or `nullptr` if not found.
 */
SHAMapLeafNode*
SHAMap::findKey(uint256 const& id) const
{
    SHAMapLeafNode* leaf = walkTowardsKey(id);  // NOLINT(misc-const-correctness)
    if ((leaf != nullptr) && leaf->peekItem()->key() != id)
        leaf = nullptr;
    return leaf;
}

/** Fetch a node from the NodeStore by hash, bypassing the in-process cache.
 *
 *  Calls `f_.db().fetchNodeObject()` then delegates to `finishFetch()` to
 *  deserialize and canonicalize the result.
 *
 *  @param hash  Hash of the node to retrieve.
 *  @return The deserialized node, or an empty pointer if not found or if
 *      deserialization fails.
 *  @note Only valid for backed maps (`backed_ == true`).
 */
intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMap::fetchNodeFromDB(SHAMapHash const& hash) const
{
    XRPL_ASSERT(backed_, "xrpl::SHAMap::fetchNodeFromDB : is backed");
    auto obj = f_.db().fetchNodeObject(hash.asUInt256(), ledgerSeq_);
    return finishFetch(hash, obj);
}

/** Deserialize a raw NodeObject into a canonicalized tree node.
 *
 *  If `object` is null, clears `full_` and notifies the missing-node
 *  acquisition pipeline via `f_.missingNodeAcquireBySeq()`, then returns an
 *  empty pointer.  Otherwise, deserializes via `SHAMapTreeNode::makeFromPrefix`,
 *  calls `canonicalize()`, and returns the result.
 *
 *  Deserialization failures (`std::runtime_error` or any other exception) are
 *  caught, logged at warn level, and suppressed — callers receive an empty
 *  pointer rather than a crash.
 *
 *  @param hash    Expected hash of the node (used for deserialization and
 *      cache registration).
 *  @param object  Raw NodeObject from the database, or null on a miss.
 *  @return The deserialized and canonicalized node, or an empty pointer on any
 *      failure.
 */
intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMap::finishFetch(SHAMapHash const& hash, std::shared_ptr<NodeObject> const& object) const
{
    XRPL_ASSERT(backed_, "xrpl::SHAMap::finishFetch : is backed");

    try
    {
        if (!object)
        {
            if (full_)
            {
                full_ = false;
                f_.missingNodeAcquireBySeq(ledgerSeq_, hash.asUInt256());
            }
            return {};
        }

        auto node = SHAMapTreeNode::makeFromPrefix(makeSlice(object->getData()), hash);
        if (node)
            canonicalize(hash, node);
        return node;
    }
    catch (std::runtime_error const& e)
    {
        JLOG(journal_.warn()) << "finishFetch exception: " << e.what();
    }
    catch (...)
    {
        JLOG(journal_.warn()) << "finishFetch exception: unknown exception: " << hash;
    }

    return {};
}

/** Attempt to supply a missing node from a `SHAMapSyncFilter`.
 *
 *  Calls `filter->getNode(hash)`.  If the filter provides data, the blob is
 *  deserialized and, for backed maps, canonicalized.  On success, notifies the
 *  filter via `gotNode(true, ...)` so it can persist or account for the node.
 *  Deserialization exceptions are caught and logged; an empty pointer is
 *  returned on any failure.
 *
 *  @param hash    Hash of the node to retrieve.
 *  @param filter  The sync filter to consult; must not be null.
 *  @return The deserialized node, or an empty pointer if the filter has no data
 *      or if deserialization fails.
 */
intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMap::checkFilter(SHAMapHash const& hash, SHAMapSyncFilter* filter) const
{
    if (auto nodeData = filter->getNode(hash))
    {
        try
        {
            auto node = SHAMapTreeNode::makeFromPrefix(makeSlice(*nodeData), hash);
            if (node)
            {
                filter->gotNode(true, hash, ledgerSeq_, std::move(*nodeData), node->getType());
                if (backed_)
                    canonicalize(hash, node);
            }
            return node;
        }
        catch (std::exception const& x)
        {
            JLOG(f_.journal().warn()) << "Invalid node/data, hash=" << hash << ": " << x.what();
        }
    }
    return {};
}

/** Retrieve a node without throwing, consulting the filter as a fallback.
 *
 *  Tiered lookup: (1) in-process `TreeNodeCache`, (2) NodeStore (backed maps
 *  only), (3) the provided `SHAMapSyncFilter`.  Returns an empty pointer on a
 *  complete miss; never throws `SHAMapMissingNode`.
 *
 *  @param hash    Hash of the node to retrieve.
 *  @param filter  Optional sync filter consulted after a database miss; may be
 *      null to skip filter lookup.
 *  @return The node if found, or an empty pointer on miss.
 */
intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMap::fetchNodeNT(SHAMapHash const& hash, SHAMapSyncFilter* filter) const
{
    auto node = cacheLookup(hash);
    if (node)
        return node;

    if (backed_)
    {
        node = fetchNodeFromDB(hash);
        if (node)
        {
            canonicalize(hash, node);
            return node;
        }
    }

    if (filter != nullptr)
        node = checkFilter(hash, filter);

    return node;
}

/** Retrieve a node without throwing, using only the cache and NodeStore.
 *
 *  Two-tier lookup: (1) in-process `TreeNodeCache`, (2) NodeStore (backed maps
 *  only).  No sync filter is consulted.  Returns an empty pointer on miss.
 *
 *  @param hash  Hash of the node to retrieve.
 *  @return The node if found, or an empty pointer on miss.
 */
intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMap::fetchNodeNT(SHAMapHash const& hash) const
{
    auto node = cacheLookup(hash);

    if (!node && backed_)
        node = fetchNodeFromDB(hash);

    return node;
}

/** Retrieve a node, throwing if it is missing.
 *
 *  Delegates to `fetchNodeNT(hash)` and throws `SHAMapMissingNode` if the
 *  result is an empty pointer.  Use this on paths that assume the tree is
 *  locally complete.
 *
 *  @param hash  Hash of the required node.
 *  @return The node.
 *  @throws SHAMapMissingNode if the node cannot be found.
 */
intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMap::fetchNode(SHAMapHash const& hash) const
{
    auto node = fetchNodeNT(hash);

    if (!node)
        Throw<SHAMapMissingNode>(type_, hash);

    return node;
}

/** Descend into `branch` of `parent`, throwing on a non-empty missing node.
 *
 *  Returns `nullptr` only when the branch is empty; throws `SHAMapMissingNode`
 *  when the branch is non-empty but the child cannot be fetched.
 *
 *  @param parent  The inner node to descend from (raw pointer variant).
 *  @param branch  Branch index (0–15) to follow.
 *  @return Raw pointer to the child, or `nullptr` for an empty branch.
 *  @throws SHAMapMissingNode if the branch is non-empty but unfetchable.
 */
SHAMapTreeNode*
SHAMap::descendThrow(SHAMapInnerNode* parent, int branch) const
{
    SHAMapTreeNode* ret = descend(parent, branch);  // NOLINT(misc-const-correctness)

    if ((ret == nullptr) && !parent->isEmptyBranch(branch))
        Throw<SHAMapMissingNode>(type_, parent->getChildHash(branch));

    return ret;
}

/** Descend into `branch` of `parent`, throwing on a non-empty missing node.
 *
 *  Shared-pointer variant of the same operation.  Returns an empty pointer only
 *  when the branch is empty; throws `SHAMapMissingNode` otherwise.
 *
 *  @param parent  The inner node to descend from (reference variant).
 *  @param branch  Branch index (0–15) to follow.
 *  @return Shared pointer to the child, or empty for an empty branch.
 *  @throws SHAMapMissingNode if the branch is non-empty but unfetchable.
 */
intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMap::descendThrow(SHAMapInnerNode& parent, int branch) const
{
    intr_ptr::SharedPtr<SHAMapTreeNode> ret = descend(parent, branch);

    if (!ret && !parent.isEmptyBranch(branch))
        Throw<SHAMapMissingNode>(type_, parent.getChildHash(branch));

    return ret;
}

/** Fetch and link a child node into its parent, returning a raw pointer.
 *
 *  If the child is already in memory (`getChildPointer` non-null) it is
 *  returned immediately.  Otherwise, for backed maps, the node is fetched from
 *  the cache or NodeStore and installed via `canonicalizeChild`.  Returns
 *  `nullptr` on miss.
 *
 *  @param parent  The inner node whose child to retrieve.
 *  @param branch  Branch index (0–15).
 *  @return Raw pointer to the child, or `nullptr` if unavailable.
 */
SHAMapTreeNode*
SHAMap::descend(SHAMapInnerNode* parent, int branch) const
{
    SHAMapTreeNode* ret = parent->getChildPointer(branch);  // NOLINT(misc-const-correctness)
    if ((ret != nullptr) || !backed_)
        return ret;

    intr_ptr::SharedPtr<SHAMapTreeNode> node = fetchNodeNT(parent->getChildHash(branch));
    if (!node)
        return nullptr;

    node = parent->canonicalizeChild(branch, std::move(node));
    return node.get();
}

/** Fetch and link a child node into its parent, returning a shared pointer.
 *
 *  Shared-pointer variant.  For backed maps, uses the throwing `fetchNode()`
 *  so a non-empty missing child raises `SHAMapMissingNode`.  Installs the
 *  result via `canonicalizeChild`.
 *
 *  @param parent  The inner node whose child to retrieve (reference variant).
 *  @param branch  Branch index (0–15).
 *  @return Shared pointer to the child, or empty if the branch is empty or
 *      the map is unbacked.
 *  @throws SHAMapMissingNode if backed and the child cannot be fetched.
 */
intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMap::descend(SHAMapInnerNode& parent, int branch) const
{
    intr_ptr::SharedPtr<SHAMapTreeNode> node = parent.getChild(branch);
    if (node || !backed_)
        return node;

    node = fetchNode(parent.getChildHash(branch));
    if (!node)
        return {};

    node = parent.canonicalizeChild(branch, std::move(node));
    return node;
}

/** Fetch a child node without installing it into the parent.
 *
 *  Returns the in-memory child if present, or fetches from the NodeStore for
 *  backed maps, but does NOT call `canonicalizeChild` — the returned node is
 *  not hooked into the tree.  Used by `walkMap` / `walkMapParallel` to probe
 *  node availability without side effects.
 *
 *  @param parent  The inner node whose child to probe.
 *  @param branch  Branch index (0–15).
 *  @return The child node if available, or an empty pointer on miss.
 *  @throws SHAMapMissingNode if backed and the child is non-empty but absent.
 */
intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMap::descendNoStore(SHAMapInnerNode& parent, int branch) const
{
    intr_ptr::SharedPtr<SHAMapTreeNode> ret = parent.getChild(branch);
    if (!ret && backed_)
        ret = fetchNode(parent.getChildHash(branch));
    return ret;
}

/** Descend to `branch` using a sync filter, returning the child and its ID.
 *
 *  Used during missing-node discovery.  If the child is not in memory, consults
 *  `filter` (via `fetchNodeNT`) then installs via `canonicalizeChild`.  The
 *  returned `SHAMapNodeID` is derived from `parentID.getChildNodeID(branch)`.
 *
 *  @param parent    Inner node to descend from.
 *  @param parentID  Trie address of `parent`.
 *  @param branch    Branch index (0–15); must be non-empty.
 *  @param filter    Sync filter supplying peer-sourced nodes; may be null.
 *  @return Pair of (child pointer or null, child node ID).
 */
std::pair<SHAMapTreeNode*, SHAMapNodeID>
SHAMap::descend(
    SHAMapInnerNode* parent,
    SHAMapNodeID const& parentID,
    int branch,
    SHAMapSyncFilter* filter) const
{
    XRPL_ASSERT(parent->isInner(), "xrpl::SHAMap::descend : valid parent input");
    XRPL_ASSERT(
        (branch >= 0) && (branch < kBRANCH_FACTOR), "xrpl::SHAMap::descend : valid branch input");
    XRPL_ASSERT(
        !parent->isEmptyBranch(branch), "xrpl::SHAMap::descend : parent branch is non-empty");

    SHAMapTreeNode* child = parent->getChildPointer(branch);  // NOLINT(misc-const-correctness)

    if (child == nullptr)
    {
        auto const& childHash = parent->getChildHash(branch);
        intr_ptr::SharedPtr<SHAMapTreeNode> childNode = fetchNodeNT(childHash, filter);

        if (childNode)
        {
            childNode = parent->canonicalizeChild(branch, std::move(childNode));
            child = childNode.get();
        }
    }

    return std::make_pair(child, parentID.getChildNodeID(branch));
}

/** Descend asynchronously, posting an I/O request when the node is absent.
 *
 *  Returns the child immediately if it is already in memory or in the filter.
 *  If the child must be loaded from the NodeStore, posts an async fetch via
 *  `f_.db().asyncFetch()`, sets `pending = true`, and returns `nullptr`.  When
 *  the fetch completes, `callback` is invoked with the deserialized node and its
 *  hash.  Used by `getMissingNodes` to maximize I/O concurrency.
 *
 *  @param parent    Inner node to descend from.
 *  @param branch    Branch index (0–15); callers must ensure non-empty.
 *  @param filter    Optional sync filter consulted before the async path.
 *  @param pending   Set to `true` if an async I/O was posted; `false`
 *      otherwise.
 *  @param callback  Invoked on async completion with `(node, hash)`.
 *  @return The child if synchronously available, or `nullptr` if async.
 */
SHAMapTreeNode*
SHAMap::descendAsync(
    SHAMapInnerNode* parent,
    int branch,
    SHAMapSyncFilter* filter,
    bool& pending,
    descendCallback&& callback) const
{
    pending = false;

    SHAMapTreeNode* ret = parent->getChildPointer(branch);  // NOLINT(misc-const-correctness)
    if (ret != nullptr)
        return ret;

    auto const& hash = parent->getChildHash(branch);

    auto ptr = cacheLookup(hash);
    if (!ptr)
    {
        if (filter != nullptr)
            ptr = checkFilter(hash, filter);

        if (!ptr && backed_)
        {
            f_.db().asyncFetch(
                hash.asUInt256(),
                ledgerSeq_,
                [this, hash, cb{std::move(callback)}](std::shared_ptr<NodeObject> const& object) {
                    auto node = finishFetch(hash, object);
                    cb(node, hash);
                });
            pending = true;
            return nullptr;
        }
    }

    if (ptr)
        ptr = parent->canonicalizeChild(branch, std::move(ptr));

    return ptr.get();
}

/** Ensure exclusive ownership of `node` before mutation (copy-on-write).
 *
 *  If `node->cowid() != cowid_`, the node is shared with another map
 *  generation and must be cloned before it can be modified.  The clone receives
 *  this map's `cowid_` and, if the node is the root, `root_` is updated to
 *  point at the clone.
 *
 *  @tparam Node  Either `SHAMapInnerNode` or `SHAMapLeafNode`.
 *  @param node    The node to potentially clone.
 *  @param nodeID  Trie address of `node`; used to detect the root.
 *  @return The original node if already exclusively owned, otherwise the clone.
 */
template <class Node>
intr_ptr::SharedPtr<Node>
SHAMap::unshareNode(intr_ptr::SharedPtr<Node> node, SHAMapNodeID const& nodeID)
{
    XRPL_ASSERT(node->cowid() <= cowid_, "xrpl::SHAMap::unshareNode : node valid for cowid");
    if (node->cowid() != cowid_)
    {
        // have a CoW
        XRPL_ASSERT(state_ != SHAMapState::Immutable, "xrpl::SHAMap::unshareNode : not immutable");
        node = intr_ptr::staticPointerCast<Node>(node->clone(cowid_));
        if (nodeID.isRoot())
            root_ = node;
    }
    return node;
}

/** Directional traversal helper shared by `firstBelow` and `lastBelow`.
 *
 *  Descends from `node` following the first non-empty branch in the direction
 *  specified by `loopParams` at each level.  Pushes every visited inner node
 *  (and ultimately the leaf) onto `stack`.  Returns `nullptr` if the subtree
 *  is empty.
 *
 *  @param node        Starting node; may be a leaf (returns it immediately) or
 *      an inner node.
 *  @param stack       Accumulates visited nodes for subsequent iterator steps.
 *  @param branch      Branch index used to compute the child `SHAMapNodeID`
 *      relative to `stack.top()`.
 *  @param loopParams  Tuple of `{init, cmp, incr}` lambdas controlling scan
 *      direction: `init` is the starting branch index, `cmp` is the loop
 *      condition, and `incr` advances the index.
 *  @return Pointer to the first/last leaf in the subtree, or `nullptr` if none.
 */
SHAMapLeafNode*
SHAMap::belowHelper(
    intr_ptr::SharedPtr<SHAMapTreeNode> node,
    SharedPtrNodeStack& stack,
    int branch,
    std::tuple<int, std::function<bool(int)>, std::function<void(int&)>> const& loopParams) const
{
    auto& [init, cmp, incr] = loopParams;
    if (node->isLeaf())
    {
        auto n = intr_ptr::staticPointerCast<SHAMapLeafNode>(node);
        stack.push({node, {kLEAF_DEPTH, n->peekItem()->key()}});
        return n.get();
    }
    auto inner = intr_ptr::staticPointerCast<SHAMapInnerNode>(node);
    if (stack.empty())
    {
        stack.emplace(inner, SHAMapNodeID{});
    }
    else
    {
        stack.emplace(inner, stack.top().second.getChildNodeID(branch));
    }
    for (int i = init; cmp(i);)
    {
        if (!inner->isEmptyBranch(i))
        {
            node.adopt(descendThrow(inner.get(), i));
            XRPL_ASSERT(!stack.empty(), "xrpl::SHAMap::belowHelper : non-empty stack");
            if (node->isLeaf())
            {
                auto n = intr_ptr::staticPointerCast<SHAMapLeafNode>(node);
                stack.push({n, {kLEAF_DEPTH, n->peekItem()->key()}});
                return n.get();
            }
            inner = intr_ptr::staticPointerCast<SHAMapInnerNode>(node);
            stack.emplace(inner, stack.top().second.getChildNodeID(branch));
            i = init;  // descend and reset loop
        }
        else
        {
            incr(i);  // scan next branch
        }
    }
    return nullptr;
}
/** Return the lexicographically last leaf at or below `node`.
 *
 *  Scans branches from 15 down to 0 at each level, descending into the first
 *  non-empty one.  Pushes visited nodes onto `stack`.
 *
 *  @param node    Subtree root to search.
 *  @param stack   Accumulates the path for iterator support.
 *  @param branch  Branch index of `node` within its parent; used to compute
 *      `SHAMapNodeID` for stack entries.
 *  @return The last leaf, or `nullptr` if the subtree is empty.
 */
SHAMapLeafNode*
SHAMap::lastBelow(intr_ptr::SharedPtr<SHAMapTreeNode> node, SharedPtrNodeStack& stack, int branch)
    const
{
    auto init = kBRANCH_FACTOR - 1;
    auto cmp = [](int i) { return i >= 0; };
    auto incr = [](int& i) { --i; };

    return belowHelper(node, stack, branch, {init, cmp, incr});
}
/** Return the lexicographically first leaf at or below `node`.
 *
 *  Scans branches from 0 up to 15 at each level, descending into the first
 *  non-empty one.  Pushes visited nodes onto `stack`.
 *
 *  @param node    Subtree root to search.
 *  @param stack   Accumulates the path for iterator support.
 *  @param branch  Branch index of `node` within its parent; used to compute
 *      `SHAMapNodeID` for stack entries.
 *  @return The first leaf, or `nullptr` if the subtree is empty.
 */
SHAMapLeafNode*
SHAMap::firstBelow(intr_ptr::SharedPtr<SHAMapTreeNode> node, SharedPtrNodeStack& stack, int branch)
    const
{
    auto init = 0;
    auto cmp = [](int i) { return i <= kBRANCH_FACTOR; };
    auto incr = [](int& i) { ++i; };

    return belowHelper(node, stack, branch, {init, cmp, incr});
}
static boost::intrusive_ptr<SHAMapItem const> const kNO_ITEM;

/** Return the sole item below `node`, or a null reference if there are many.
 *
 *  Traverses downward following the single occupied branch at each level.  If
 *  at any level more than one branch is occupied, returns `kNO_ITEM`.  Used
 *  during deletion to determine whether an inner node can be collapsed.
 *
 *  @param node  Subtree root to examine.
 *  @return Reference to the unique `SHAMapItem` if exactly one leaf exists
 *      below `node`, or `kNO_ITEM` if there are zero or more than one.
 */
boost::intrusive_ptr<SHAMapItem const> const&
SHAMap::onlyBelow(SHAMapTreeNode* node) const
{
    while (!node->isLeaf())
    {
        SHAMapTreeNode* nextNode = nullptr;
        auto inner = safeDowncast<SHAMapInnerNode*>(node);
        for (int i = 0; i < kBRANCH_FACTOR; ++i)
        {
            if (!inner->isEmptyBranch(i))
            {
                if (nextNode != nullptr)
                    return kNO_ITEM;

                nextNode = descendThrow(inner, i);
            }
        }

        if (nextNode == nullptr)
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::SHAMap::onlyBelow : no next node");
            return kNO_ITEM;
            // LCOV_EXCL_STOP
        }

        node = nextNode;
    }

    // An inner node must have at least one leaf
    // below it, unless it's the root_
    auto const leaf = safeDowncast<SHAMapLeafNode const*>(node);
    XRPL_ASSERT(
        leaf->peekItem() || (leaf == root_.get()), "xrpl::SHAMap::onlyBelow : valid inner node");
    return leaf->peekItem();
}

/** Return a pointer to the first leaf and initialise the iterator stack.
 *
 *  Calls `firstBelow(root_, stack)`.  On an empty map, clears `stack` and
 *  returns `nullptr`.  Used as the entry point for forward iteration.
 *
 *  @param stack  Must be empty on entry; populated with the path to the first
 *      leaf on return.
 *  @return Pointer to the first leaf, or `nullptr` if the map is empty.
 */
SHAMapLeafNode const*
SHAMap::peekFirstItem(SharedPtrNodeStack& stack) const
{
    XRPL_ASSERT(stack.empty(), "xrpl::SHAMap::peekFirstItem : empty stack input");
    SHAMapLeafNode const* node = firstBelow(root_, stack);
    if (node == nullptr)
    {
        while (!stack.empty())
            stack.pop();
        return nullptr;
    }
    return node;
}

/** Advance the iterator to the next leaf in ascending key order.
 *
 *  Pops the current leaf from `stack`, then walks up the stack popping inner
 *  nodes until a node with a non-empty branch after the branch taken to reach
 *  `id` is found.  Descends into that branch via `firstBelow`.  Returns
 *  `nullptr` when `id` was the last item.
 *
 *  @param id     Key of the current leaf (used to identify which branch was
 *      last taken at each level).
 *  @param stack  Path from the previous `peekFirstItem` / `peekNextItem` call;
 *      updated in place.
 *  @return Pointer to the next leaf, or `nullptr` if iteration is exhausted.
 *  @throws SHAMapMissingNode if a required node cannot be fetched.
 */
SHAMapLeafNode const*
SHAMap::peekNextItem(uint256 const& id, SharedPtrNodeStack& stack) const
{
    XRPL_ASSERT(!stack.empty(), "xrpl::SHAMap::peekNextItem : non-empty stack input");
    XRPL_ASSERT(stack.top().first->isLeaf(), "xrpl::SHAMap::peekNextItem : stack starts with leaf");
    stack.pop();
    while (!stack.empty())
    {
        auto [node, nodeID] = stack.top();
        XRPL_ASSERT(!node->isLeaf(), "xrpl::SHAMap::peekNextItem : another node is not leaf");
        auto inner = intr_ptr::staticPointerCast<SHAMapInnerNode>(node);
        for (auto i = selectBranch(nodeID, id) + 1; i < kBRANCH_FACTOR; ++i)
        {
            if (!inner->isEmptyBranch(i))
            {
                node = descendThrow(*inner, i);
                auto leaf = firstBelow(node, stack, i);
                if (leaf == nullptr)
                    Throw<SHAMapMissingNode>(type_, id);
                XRPL_ASSERT(leaf->isLeaf(), "xrpl::SHAMap::peekNextItem : leaf is valid");
                return leaf;
            }
        }
        stack.pop();
    }
    return nullptr;
}

/** Look up an item by key without transferring ownership.
 *
 *  Returns a reference to the intrusive pointer stored in the leaf.  The
 *  reference is valid as long as the map is not mutated.  Returns `kNO_ITEM`
 *  (a null intrusive pointer) if `id` is not present.
 *
 *  @param id  Key to look up.
 *  @return Reference to the item pointer, or a reference to `kNO_ITEM`.
 */
boost::intrusive_ptr<SHAMapItem const> const&
SHAMap::peekItem(uint256 const& id) const
{
    SHAMapLeafNode const* leaf = findKey(id);

    if (leaf == nullptr)
        return kNO_ITEM;

    return leaf->peekItem();
}

/** Look up an item by key and also retrieve its leaf hash.
 *
 *  @param id    Key to look up.
 *  @param hash  Populated with the leaf node's hash on success; unchanged on
 *      miss.
 *  @return Reference to the item pointer, or a reference to `kNO_ITEM`.
 */
boost::intrusive_ptr<SHAMapItem const> const&
SHAMap::peekItem(uint256 const& id, SHAMapHash& hash) const
{
    SHAMapLeafNode const* leaf = findKey(id);

    if (leaf == nullptr)
        return kNO_ITEM;

    hash = leaf->getHash();
    return leaf->peekItem();
}

/** Return an iterator to the first item whose key is strictly greater than `id`.
 *
 *  Walks towards `id` to build a path stack, then unwinds upward scanning for
 *  the next occupied branch after the one that leads to `id`.  Descends into
 *  that branch via `firstBelow`.  Returns `end()` if no such item exists.
 *
 *  @param id  Pivot key; does not need to be present in the map.
 *  @return Iterator to the first item with `key > id`, or `end()`.
 *  @throws SHAMapMissingNode if a required node cannot be fetched.
 */
SHAMap::ConstIterator
SHAMap::upperBound(uint256 const& id) const
{
    SharedPtrNodeStack stack;
    walkTowardsKey(id, &stack);
    while (!stack.empty())
    {
        auto [node, nodeID] = stack.top();
        if (node->isLeaf())
        {
            auto leaf = safeDowncast<SHAMapLeafNode*>(node.get());
            if (leaf->peekItem()->key() > id)
                return ConstIterator(this, leaf->peekItem().get(), std::move(stack));
        }
        else
        {
            auto inner = intr_ptr::staticPointerCast<SHAMapInnerNode>(node);
            for (auto branch = selectBranch(nodeID, id) + 1; branch < kBRANCH_FACTOR; ++branch)
            {
                if (!inner->isEmptyBranch(branch))
                {
                    node = descendThrow(*inner, branch);
                    auto leaf = firstBelow(node, stack, branch);
                    if (leaf == nullptr)
                        Throw<SHAMapMissingNode>(type_, id);
                    return ConstIterator(this, leaf->peekItem().get(), std::move(stack));
                }
            }
        }
        stack.pop();
    }
    return end();
}
/** Return an iterator to the last item whose key is strictly less than `id`.
 *
 *  Walks towards `id`, then unwinds upward scanning for the next occupied
 *  branch *before* the one leading to `id`, descending via `lastBelow`.
 *  Returns `end()` if no such item exists.
 *
 *  @param id  Pivot key; does not need to be present in the map.
 *  @return Iterator to the greatest item with `key < id`, or `end()`.
 *  @throws SHAMapMissingNode if a required node cannot be fetched.
 *  @note This is a reverse lower-bound, not the STL convention; it finds the
 *      item just *below* `id`, not just at-or-above it.
 */
SHAMap::ConstIterator
SHAMap::lowerBound(uint256 const& id) const
{
    SharedPtrNodeStack stack;
    walkTowardsKey(id, &stack);
    while (!stack.empty())
    {
        auto [node, nodeID] = stack.top();
        if (node->isLeaf())
        {
            auto leaf = safeDowncast<SHAMapLeafNode*>(node.get());
            if (leaf->peekItem()->key() < id)
                return ConstIterator(this, leaf->peekItem().get(), std::move(stack));
        }
        else
        {
            auto inner = intr_ptr::staticPointerCast<SHAMapInnerNode>(node);
            for (int branch = selectBranch(nodeID, id) - 1; branch >= 0; --branch)
            {
                if (!inner->isEmptyBranch(branch))
                {
                    node = descendThrow(*inner, branch);
                    auto leaf = lastBelow(node, stack, branch);
                    if (leaf == nullptr)
                        Throw<SHAMapMissingNode>(type_, id);
                    return ConstIterator(this, leaf->peekItem().get(), std::move(stack));
                }
            }
        }
        stack.pop();
    }
    // TODO: what to return here?
    return end();
}

bool
SHAMap::hasItem(uint256 const& id) const
{
    return (findKey(id) != nullptr);
}

/** Remove the item with key `id` from the map.
 *
 *  Walks to the target leaf, then unwinds the path stack reducing inner-node
 *  child counts.  Nodes that drop to zero children are nulled out; nodes that
 *  drop to one child are collapsed — the surviving leaf is hoisted upward to
 *  enforce the radix-trie merge property.  Finally, `dirtyUp` links the
 *  modified subtree back to the root.
 *
 *  @param id  Key of the item to remove.
 *  @return `true` if the item was found and removed; `false` if not present.
 *  @throws SHAMapMissingNode if the tree is incomplete and a required node
 *      cannot be fetched.
 */
bool
SHAMap::delItem(uint256 const& id)
{
    XRPL_ASSERT(state_ != SHAMapState::Immutable, "xrpl::SHAMap::delItem : not immutable");

    SharedPtrNodeStack stack;
    walkTowardsKey(id, &stack);

    if (stack.empty())
        Throw<SHAMapMissingNode>(type_, id);

    auto leaf = intr_ptr::dynamicPointerCast<SHAMapLeafNode>(stack.top().first);
    stack.pop();

    if (!leaf || (leaf->peekItem()->key() != id))
        return false;

    SHAMapNodeType const type = leaf->getType();

    using TreeNodeType = intr_ptr::SharedPtr<SHAMapTreeNode>;

    // prevNode starts null: the deleted leaf produces nothing to attach upward.
    TreeNodeType prevNode;

    while (!stack.empty())
    {
        auto node = intr_ptr::staticPointerCast<SHAMapInnerNode>(stack.top().first);
        SHAMapNodeID const nodeID = stack.top().second;
        stack.pop();

        node = unshareNode(std::move(node), nodeID);
        node->setChild(
            selectBranch(nodeID, id), std::move(prevNode));  // NOLINT(bugprone-use-after-move)

        XRPL_ASSERT(
            not prevNode,  // NOLINT(bugprone-use-after-move)
            "xrpl::SHAMap::delItem : prevNode should be nullptr after std::move");

        if (!nodeID.isRoot())
        {
            int const bc = node->getBranchCount();
            if (bc == 0)
            {
                // Note: This is unnecessary due to the std::move above but left here for safety
                prevNode = TreeNodeType{};
            }
            else if (bc == 1)
            {
                auto item = onlyBelow(node.get());

                if (item)
                {
                    for (int i = 0; i < kBRANCH_FACTOR; ++i)
                    {
                        if (!node->isEmptyBranch(i))
                        {
                            node->setChild(i, TreeNodeType{});
                            break;
                        }
                    }

                    prevNode = makeTypedLeaf(type, item, node->cowid());
                }
                else
                {
                    prevNode = std::move(node);
                }
            }
            else
            {
                prevNode = std::move(node);
            }
        }
    }

    return true;
}

/** Insert a new item into the map; does not update an existing item.
 *
 *  Two cases: (1) the walk terminates at an empty inner-node branch — the item
 *  is placed there directly; (2) the walk terminates at an existing leaf whose
 *  key prefix-collides with the new key — a chain of new inner nodes is created
 *  descending until the two keys diverge into separate branches.  This
 *  preserves the radix-trie merge property (inner nodes only where ≥2 items
 *  share a prefix).
 *
 *  @param type  Leaf type to create; must not be `TnInner`.
 *  @param item  The item to insert; its `key()` must not already be present.
 *  @return `true` if inserted; `false` if the key already exists.
 *  @throws SHAMapMissingNode if the tree is incomplete during traversal.
 */
bool
SHAMap::addGiveItem(SHAMapNodeType type, boost::intrusive_ptr<SHAMapItem const> item)
{
    XRPL_ASSERT(state_ != SHAMapState::Immutable, "xrpl::SHAMap::addGiveItem : not immutable");
    XRPL_ASSERT(type != SHAMapNodeType::TnInner, "xrpl::SHAMap::addGiveItem : valid type input");
    uint256 const tag = item->key();

    SharedPtrNodeStack stack;
    walkTowardsKey(tag, &stack);

    if (stack.empty())
        Throw<SHAMapMissingNode>(type_, tag);

    auto [node, nodeID] = stack.top();
    stack.pop();

    if (node->isLeaf())
    {
        auto leaf = intr_ptr::staticPointerCast<SHAMapLeafNode>(node);
        if (leaf->peekItem()->key() == tag)
            return false;
    }
    node = unshareNode(std::move(node), nodeID);
    if (node->isInner())
    {
        auto inner = intr_ptr::staticPointerCast<SHAMapInnerNode>(node);
        int const branch = selectBranch(nodeID, tag);
        XRPL_ASSERT(
            inner->isEmptyBranch(branch), "xrpl::SHAMap::addGiveItem : inner branch is empty");
        inner->setChild(branch, makeTypedLeaf(type, std::move(item), cowid_));
    }
    else
    {
        auto leaf = intr_ptr::staticPointerCast<SHAMapLeafNode>(node);
        auto otherItem = leaf->peekItem();
        XRPL_ASSERT(
            otherItem && (tag != otherItem->key()), "xrpl::SHAMap::addGiveItem : non-null item");

        node = intr_ptr::makeShared<SHAMapInnerNode>(node->cowid());

        unsigned int b1 = 0, b2 = 0;

        while ((b1 = selectBranch(nodeID, tag)) == (b2 = selectBranch(nodeID, otherItem->key())))
        {
            stack.emplace(node, nodeID);
            nodeID = nodeID.getChildNodeID(b1);
            node = intr_ptr::makeShared<SHAMapInnerNode>(cowid_);
        }

        XRPL_ASSERT(node->isInner(), "xrpl::SHAMap::addGiveItem : node is inner");

        auto inner = safeDowncast<SHAMapInnerNode*>(node.get());
        inner->setChild(b1, makeTypedLeaf(type, std::move(item), cowid_));
        inner->setChild(b2, makeTypedLeaf(type, std::move(otherItem), cowid_));
    }

    dirtyUp(stack, tag, node);
    return true;
}

/** Insert a new item into the map (forwarding wrapper for `addGiveItem`).
 *
 *  @param type  Leaf type to create.
 *  @param item  The item to insert.
 *  @return `true` if inserted; `false` if the key already exists.
 */
bool
SHAMap::addItem(SHAMapNodeType type, boost::intrusive_ptr<SHAMapItem const> item)
{
    return addGiveItem(type, std::move(item));
}

/** Return the Merkle root hash, computing it if necessary.
 *
 *  If the root's stored hash is zero (indicating pending mutations), calls
 *  `unshare()` to perform a full hash recompute.  This requires a
 *  `const_cast` because hash propagation is logically a read (the content does
 *  not change) but physically mutates inner nodes.
 *
 *  @return The current root hash.
 */
SHAMapHash
SHAMap::getHash() const
{
    auto hash = root_->getHash();
    if (hash.isZero())
    {
        const_cast<SHAMap&>(*this).unshare();
        hash = root_->getHash();
    }
    return hash;
}

/** Replace the payload of an existing item, keeping its key unchanged.
 *
 *  Locates the leaf for `item->key()`, CoW-unshares it, and calls `setItem()`.
 *  `dirtyUp()` is invoked only when `setItem()` signals the hash actually
 *  changed, preventing spurious rehashing for no-op updates.  Cross-type
 *  changes (e.g., updating a transaction leaf as an account-state leaf) are
 *  rejected with a fatal log and a `false` return.
 *
 *  @param type  Must match the existing leaf's type.
 *  @param item  New payload; `item->key()` must already be present.
 *  @return `true` on success; `false` if the key is absent or the type differs.
 */
bool
SHAMap::updateGiveItem(SHAMapNodeType type, boost::intrusive_ptr<SHAMapItem const> item)
{
    uint256 const tag = item->key();

    XRPL_ASSERT(state_ != SHAMapState::Immutable, "xrpl::SHAMap::updateGiveItem : not immutable");

    SharedPtrNodeStack stack;
    walkTowardsKey(tag, &stack);

    if (stack.empty())
        Throw<SHAMapMissingNode>(type_, tag);

    auto node = intr_ptr::dynamicPointerCast<SHAMapLeafNode>(stack.top().first);
    auto nodeID = stack.top().second;
    stack.pop();

    if (!node || (node->peekItem()->key() != tag))
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::SHAMap::updateGiveItem : invalid node");
        return false;
        // LCOV_EXCL_STOP
    }

    if (node->getType() != type)
    {
        JLOG(journal_.fatal()) << "SHAMap::updateGiveItem: cross-type change!";
        return false;
    }

    node = unshareNode(std::move(node), nodeID);

    if (node->setItem(item))
        dirtyUp(stack, tag, node);

    return true;
}

/** Attempt to fetch and install the root node with the given hash.
 *
 *  A no-op if the current root already has the requested hash.  Otherwise
 *  tries `fetchNodeNT(hash, filter)` and, on success, replaces `root_`.
 *
 *  @param hash    Expected root hash.
 *  @param filter  Optional sync filter supplying peer-sourced data.
 *  @return `true` if the root now has `hash`; `false` if the node could not
 *      be found.
 */
bool
SHAMap::fetchRoot(SHAMapHash const& hash, SHAMapSyncFilter* filter)
{
    if (hash == root_->getHash())
        return true;

    if (auto stream = journal_.trace())
    {
        if (type_ == SHAMapType::TRANSACTION)
        {
            stream << "Fetch root TXN node " << hash;
        }
        else if (type_ == SHAMapType::STATE)
        {
            stream << "Fetch root STATE node " << hash;
        }
        else
        {
            stream << "Fetch root SHAMap node " << hash;
        }
    }

    auto newRoot = fetchNodeNT(hash, filter);

    if (newRoot)
    {
        root_ = newRoot;
        XRPL_ASSERT(root_->getHash() == hash, "xrpl::SHAMap::fetchRoot : root hash do match");
        return true;
    }

    return false;
}

/** Canonicalize a flushed node and persist it to the NodeStore.
 *
 *  The node must already have `cowid == 0` (i.e., `unshare()` must have been
 *  called) so it is safe to register in the shared `TreeNodeCache`.
 *  Serializes via `serializeWithPrefix` and stores the result via
 *  `Family::db().store()`.
 *
 *  @param t     NodeStore object type to use when persisting.
 *  @param node  The node to write; must have `cowid() == 0`.
 *  @return The node (possibly replaced with the canonical instance from the
 *      cache).
 */
intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMap::writeNode(NodeObjectType t, intr_ptr::SharedPtr<SHAMapTreeNode> node) const
{
    XRPL_ASSERT(node->cowid() == 0, "xrpl::SHAMap::writeNode : valid input node");
    XRPL_ASSERT(backed_, "xrpl::SHAMap::writeNode : is backed");

    canonicalize(node->getHash(), node);

    Serializer s;
    node->serializeWithPrefix(s);
    f_.db().store(t, std::move(s.modData()), node->getHash().asUInt256(), ledgerSeq_);
    return node;
}

/** Clone a node if needed before modifying it during a flush.
 *
 *  Flushing converts inner nodes to point at canonical/shared children,
 *  which physically mutates them.  If `node->cowid() != cowid_`, the node is
 *  shared with another map generation and must be cloned before this map can
 *  safely modify it — otherwise the modification would corrupt other maps.
 *
 *  @tparam Node  `SHAMapInnerNode` or `SHAMapLeafNode`.
 *  @param node  Node to prepare for flushing; must have non-zero `cowid()`
 *      (a zero cowid would mean it is already shared/canonical, which is a
 *      logic error since canonical nodes are never dirty).
 *  @return The original node if exclusively owned, otherwise its clone.
 */
template <class Node>
intr_ptr::SharedPtr<Node>
SHAMap::preFlushNode(intr_ptr::SharedPtr<Node> node) const
{
    XRPL_ASSERT(node->cowid(), "xrpl::SHAMap::preFlushNode : valid input node");

    if (node->cowid() != cowid_)
    {
        node = intr_ptr::staticPointerCast<Node>(node->clone(cowid_));
    }
    return node;
}

/** Traverse the entire tree making every owned node shareable (cowid → 0).
 *
 *  Calls `walkSubTree(false, ...)`.  No data is written to the NodeStore.
 *  After this call all nodes are safe to share with other maps (e.g., after
 *  creating a snapshot whose parent must not later corrupt the shared nodes).
 *
 *  @return Number of nodes processed.
 */
int
SHAMap::unshare()
{
    return walkSubTree(false, NodeObjectType::Unknown);
}

/** Flush all dirty nodes to the NodeStore and make them shareable.
 *
 *  Calls `walkSubTree(backed_, t)`.  For database-backed maps this writes
 *  every owned node; for unbacked (in-memory) maps it only performs the
 *  `unshare` step without I/O.
 *
 *  @param t  NodeStore object type to use when persisting nodes.
 *  @return Number of nodes processed.
 */
int
SHAMap::flushDirty(NodeObjectType t)
{
    return walkSubTree(backed_, t);
}

/** Post-order DFS flush: update hashes and optionally persist all dirty nodes.
 *
 *  Uses an explicit stack to avoid recursion on a tree up to 64 levels deep.
 *  For each node encountered: `preFlushNode()` clones if shared, `updateHash()`
 *  / `updateHashDeep()` recomputes the hash, and `unshare()` sets `cowid` to 0.
 *  If `doWrite` is true, `writeNode()` serializes and stores each node.  The
 *  last inner node processed becomes the new `root_`.
 *
 *  @param doWrite  If true, persist each node to the NodeStore.  Must be false
 *      for unbacked maps.
 *  @param t        NodeStore object type passed to `writeNode`.
 *  @return Total number of nodes flushed (leaves + inner nodes).
 */
int
SHAMap::walkSubTree(bool doWrite, NodeObjectType t)
{
    XRPL_ASSERT(!doWrite || backed_, "xrpl::SHAMap::walkSubTree : valid input");

    int flushed = 0;

    if (!root_ || (root_->cowid() == 0))
        return flushed;

    if (root_->isLeaf())
    {  // special case -- root_ is leaf
        root_ = preFlushNode(std::move(root_));
        root_->updateHash();
        root_->unshare();

        if (doWrite)
            root_ = writeNode(t, std::move(root_));

        return 1;
    }

    auto node = intr_ptr::staticPointerCast<SHAMapInnerNode>(root_);

    if (node->isEmpty())
    {  // replace empty root with a new empty root
        root_ = intr_ptr::makeShared<SHAMapInnerNode>(0);
        return 1;
    }

    // Stack of {parent,index,child} pointers representing
    // inner nodes we are in the process of flushing
    using StackEntry = std::pair<intr_ptr::SharedPtr<SHAMapInnerNode>, int>;
    std::stack<StackEntry, std::vector<StackEntry>> stack;

    node = preFlushNode(std::move(node));

    int pos = 0;

    // We can't flush an inner node until we flush its children (post-order).
    while (true)
    {
        while (pos < kBRANCH_FACTOR)
        {
            if (node->isEmptyBranch(pos))
            {
                ++pos;
            }
            else
            {
                // No need to do I/O. If the node isn't linked,
                // it can't need to be flushed.
                int const branch = pos;
                auto child = node->getChild(pos++);

                if (child && (child->cowid() != 0))
                {
                    child = preFlushNode(std::move(child));

                    if (child->isInner())
                    {
                        stack.emplace(std::move(node), branch);
                        node = intr_ptr::staticPointerCast<SHAMapInnerNode>(child);
                        pos = 0;
                    }
                    else
                    {
                        ++flushed;

                        XRPL_ASSERT(
                            node->cowid() == cowid_,
                            "xrpl::SHAMap::walkSubTree : node cowid do "
                            "match");
                        child->updateHash();
                        child->unshare();

                        if (doWrite)
                            child = writeNode(t, std::move(child));

                        node->shareChild(branch, child);
                    }
                }
            }
        }

        node->updateHashDeep();
        node->unshare();

        if (doWrite)
            node = intr_ptr::staticPointerCast<SHAMapInnerNode>(writeNode(t, std::move(node)));

        ++flushed;

        if (stack.empty())
            break;

        auto parent = std::move(stack.top().first);
        pos = stack.top().second;
        stack.pop();

        XRPL_ASSERT(parent->cowid() == cowid_, "xrpl::SHAMap::walkSubTree : parent cowid do match");
        parent->shareChild(pos, node);

        node = std::move(parent);
        ++pos;
    }

    // Last inner node processed becomes the new root_.
    root_ = std::move(node);

    return flushed;
}

/** Log the full tree structure to the journal at INFO level.
 *
 *  Performs an iterative DFS, printing each node's string representation.
 *  Only in-memory (already-linked) children are visited — nodes that exist
 *  only in the NodeStore are not fetched.  Intended for debugging only.
 *
 *  @param hash  If true, also log each node's hash alongside its description.
 */
void
SHAMap::dump(bool hash) const
{
    int leafCount = 0;
    JLOG(journal_.info()) << " MAP Contains";

    std::stack<std::pair<SHAMapTreeNode*, SHAMapNodeID>> stack;
    stack.emplace(root_.get(), SHAMapNodeID());

    do
    {
        auto [node, nodeID] = stack.top();
        stack.pop();

        JLOG(journal_.info()) << node->getString(nodeID);
        if (hash)
        {
            JLOG(journal_.info()) << "Hash: " << node->getHash();
        }

        if (node->isInner())
        {
            auto inner = safeDowncast<SHAMapInnerNode*>(node);
            for (int i = 0; i < kBRANCH_FACTOR; ++i)
            {
                if (!inner->isEmptyBranch(i))
                {
                    auto child = inner->getChildPointer(i);
                    if (child != nullptr)
                    {
                        XRPL_ASSERT(
                            child->getHash() == inner->getChildHash(i),
                            "xrpl::SHAMap::dump : child hash do match");
                        stack.emplace(child, nodeID.getChildNodeID(i));
                    }
                }
            }
        }
        else
        {
            ++leafCount;
        }
    } while (!stack.empty());

    JLOG(journal_.info()) << leafCount << " resident leaves";
}

/** Look up a node in the family-wide `TreeNodeCache`.
 *
 *  Any node returned from the cache has `cowid() == 0` — it is canonical and
 *  shared, and must not be mutated without first calling `unshareNode()`.
 *
 *  @param hash  Hash of the node to look up.
 *  @return The cached node, or an empty pointer on a cache miss.
 */
intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMap::cacheLookup(SHAMapHash const& hash) const
{
    auto ret = f_.getTreeNodeCache()->fetch(hash.asUInt256());
    XRPL_ASSERT(!ret || !ret->cowid(), "xrpl::SHAMap::cacheLookup : not found or zero cowid");
    return ret;
}

/** Register a node in the family-wide `TreeNodeCache`, deduplicating by hash.
 *
 *  If the cache already holds a node for `hash`, `node` is replaced with that
 *  cached instance so all maps in the same `Family` share one object per hash.
 *  If not, `node` is inserted.  The node must have `cowid() == 0`; placing a
 *  CoW-owned node in the shared cache would allow other maps to mutate it.
 *
 *  @param hash  Hash of `node`; must equal `node->getHash()`.
 *  @param node  Node to canonicalize; updated in place if replaced by cache.
 */
void
SHAMap::canonicalize(SHAMapHash const& hash, intr_ptr::SharedPtr<SHAMapTreeNode>& node) const
{
    XRPL_ASSERT(backed_, "xrpl::SHAMap::canonicalize : is backed");
    XRPL_ASSERT(node->cowid() == 0, "xrpl::SHAMap::canonicalize : valid node input");
    XRPL_ASSERT(node->getHash() == hash, "xrpl::SHAMap::canonicalize : node hash do match");

    f_.getTreeNodeCache()->canonicalizeReplaceClient(hash.asUInt256(), node);
}

/** Verify internal consistency of the entire tree.
 *
 *  Forces a full hash recompute via `getHash()`, asserts the root is a
 *  non-leaf inner node, iterates every leaf via `peekFirstItem` /
 *  `peekNextItem` to exercise all descent paths, and then delegates to
 *  `SHAMapTreeNode::invariants(true)` to verify each node's structural
 *  invariants.  Intended for use in tests and debug builds.
 */
void
SHAMap::invariants() const
{
    (void)getHash();  // update node hashes
    auto node = root_.get();
    XRPL_ASSERT(node, "xrpl::SHAMap::invariants : non-null root node");
    XRPL_ASSERT(!node->isLeaf(), "xrpl::SHAMap::invariants : root node is not leaf");
    SharedPtrNodeStack stack;
    for (auto leaf = peekFirstItem(stack); leaf != nullptr;
         leaf = peekNextItem(leaf->peekItem()->key(), stack))
        ;
    node->invariants(true);
}

}  // namespace xrpl
