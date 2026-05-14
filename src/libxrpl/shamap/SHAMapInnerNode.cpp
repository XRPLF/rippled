/** @file
 *  Implements SHAMapInnerNode — the 16-way branching (non-leaf) node of the
 *  SHAMap Merkle radix trie.
 *
 *  Each inner node fans out on 4 bits of a 256-bit key (branchFactor = 16).
 *  Child hashes and child pointers are stored together in a single sparse
 *  TaggedPointer allocation whose capacity is encoded in the pointer's two
 *  low bits.  All mutations require exclusive CoW ownership (cowid_ != 0).
 *  Concurrent reads of different children are serialised with per-child bit
 *  spinlocks packed into a single 16-bit atomic.
 */
#include <xrpl/shamap/SHAMapInnerNode.h>

#include <xrpl/basics/IntrusivePointer.h>    // IWYU pragma: keep
#include <xrpl/basics/IntrusivePointer.ipp>  // IWYU pragma: keep
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/spinlock.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapTreeNode.h>
#include <xrpl/shamap/detail/TaggedPointer.h>
#include <xrpl/shamap/detail/TaggedPointer.ipp>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace xrpl {

/** Construct an inner node with a given CoW owner and initial child-array capacity.
 *
 *  The TaggedPointer is allocated with room for @p numAllocatedChildren pairs,
 *  rounded up to the next supported capacity tier (2, 4, 8, or 16).  Starting
 *  small (the default of 2) minimises RAM for nodes that stay sparse.
 *
 *  @param cowid               Copy-on-write owner ID; 0 means shared/immutable.
 *  @param numAllocatedChildren Initial capacity of the hashes and children arrays.
 */
SHAMapInnerNode::SHAMapInnerNode(std::uint32_t cowid, std::uint8_t numAllocatedChildren)
    : SHAMapTreeNode(cowid), hashesAndChildren_(numAllocatedChildren)
{
}

SHAMapInnerNode::~SHAMapInnerNode() = default;

/** Release all child SharedPtrs before the object's memory is reclaimed.
 *
 *  Called by the intrusive reference-count infrastructure when the strong
 *  count reaches zero while weak references are still live.  Explicitly
 *  resetting every child pointer breaks reference cycles and ensures timely
 *  resource cleanup without waiting for weak references to expire.
 *
 *  @note This runs before the destructor; do not access @c hash_ or
 *      @c isBranch_ after this point.
 */
void
SHAMapInnerNode::partialDestructor()
{
    intr_ptr::SharedPtr<SHAMapTreeNode>* children = nullptr;
    // structured bindings can't be captured in c++ 17; use tie instead
    std::tie(std::ignore, std::ignore, children) = hashesAndChildren_.getHashesAndChildren();
    iterNonEmptyChildIndexes([&](auto branchNum, auto indexNum) { children[indexNum].reset(); });
}

template <class F>
void
SHAMapInnerNode::iterChildren(F&& f) const
{
    hashesAndChildren_.iterChildren(isBranch_, std::forward<F>(f));
}

template <class F>
void
SHAMapInnerNode::iterNonEmptyChildIndexes(F&& f) const
{
    hashesAndChildren_.iterNonEmptyChildIndexes(isBranch_, std::forward<F>(f));
}

void
SHAMapInnerNode::resizeChildArrays(std::uint8_t toAllocate)
{
    hashesAndChildren_ = TaggedPointer(std::move(hashesAndChildren_), isBranch_, toAllocate);
}

std::optional<int>
SHAMapInnerNode::getChildIndex(int i) const
{
    return hashesAndChildren_.getChildIndex(isBranch_, i);
}

/** Produce a CoW-owned deep copy of this node for a new owner.
 *
 *  Allocates a new SHAMapInnerNode sized exactly for the current branch
 *  count and copies all hashes and child pointers.  Hashes are copied
 *  outside the spinlock (they are immutable on shared nodes); child
 *  pointers are copied under the full-node Spinlock so that a concurrent
 *  call to canonicalizeChild() cannot race the copy.
 *
 *  Sparse and dense layouts are handled separately: a sparse source packs
 *  entries sequentially in the clone; a dense source maps branch number
 *  directly to array index so the clone is also dense.
 *
 *  @param cowid Copy-on-write owner ID assigned to the new node.
 *  @return A freshly allocated node sharing no mutable state with the
 *      original.
 */
intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMapInnerNode::clone(std::uint32_t cowid) const
{
    auto const branchCount = getBranchCount();
    auto const thisIsSparse = !hashesAndChildren_.isDense();
    auto p = intr_ptr::makeShared<SHAMapInnerNode>(cowid, branchCount);
    p->hash_ = hash_;
    p->isBranch_ = isBranch_;
    p->fullBelowGen_ = fullBelowGen_;
    SHAMapHash *cloneHashes = nullptr, *thisHashes = nullptr;
    intr_ptr::SharedPtr<SHAMapTreeNode>*cloneChildren = nullptr, *thisChildren = nullptr;
    // structured bindings can't be captured in c++ 17; use tie instead
    std::tie(std::ignore, cloneHashes, cloneChildren) =
        p->hashesAndChildren_.getHashesAndChildren();
    std::tie(std::ignore, thisHashes, thisChildren) = hashesAndChildren_.getHashesAndChildren();

    if (thisIsSparse)
    {
        int cloneChildIndex = 0;
        iterNonEmptyChildIndexes([&](auto branchNum, auto indexNum) {
            cloneHashes[cloneChildIndex++] = thisHashes[indexNum];
        });
    }
    else
    {
        iterNonEmptyChildIndexes(
            [&](auto branchNum, auto indexNum) { cloneHashes[branchNum] = thisHashes[indexNum]; });
    }

    Spinlock sl(lock_);
    std::scoped_lock const lock(sl);

    if (thisIsSparse)
    {
        int cloneChildIndex = 0;
        iterNonEmptyChildIndexes([&](auto branchNum, auto indexNum) {
            cloneChildren[cloneChildIndex++] = thisChildren[indexNum];
        });
    }
    else
    {
        iterNonEmptyChildIndexes([&](auto branchNum, auto indexNum) {
            cloneChildren[branchNum] = thisChildren[indexNum];
        });
    }

    return p;
}

/** Deserialize a full-format inner node from wire data.
 *
 *  The full format encodes all 16 child hashes in branch order (512 bytes).
 *  After parsing, the child arrays are right-sized via resizeChildArrays()
 *  to match actual occupancy, and the node hash is either adopted from
 *  @p hash (trusted path, e.g. retrieved by known hash) or recomputed.
 *
 *  @param data      Raw wire bytes; must be exactly 512 bytes (16 × 32).
 *  @param hash      Expected node hash, used only when @p hashValid is true.
 *  @param hashValid If true, adopt @p hash without recomputing; if false,
 *      call updateHash() to derive the hash from child data.
 *  @return A freshly allocated, shareable (cowid = 0) inner node.
 *  @throws std::runtime_error if @p data is not exactly 512 bytes.
 */
intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMapInnerNode::makeFullInner(Slice data, SHAMapHash const& hash, bool hashValid)
{
    // A full inner node is serialized as 16 256-bit hashes, back to back:
    if (data.size() != kBRANCH_FACTOR * uint256::kBYTES)
        Throw<std::runtime_error>("Invalid FI node");

    auto ret = intr_ptr::makeShared<SHAMapInnerNode>(0, kBRANCH_FACTOR);

    SerialIter si(data);

    auto hashes = ret->hashesAndChildren_.getHashes();

    for (int i = 0; i < kBRANCH_FACTOR; ++i)
    {
        hashes[i].asUInt256() = si.getBitString<256>();

        if (hashes[i].isNonZero())
            ret->isBranch_ |= (1 << i);
    }

    ret->resizeChildArrays(ret->getBranchCount());

    if (hashValid)
    {
        ret->hash_ = hash;
    }
    else
    {
        ret->updateHash();
    }

    return ret;
}

/** Deserialize a compressed-format inner node from wire data.
 *
 *  The compressed format encodes only non-empty branches.  Each entry is
 *  33 bytes: a 32-byte hash followed by a 1-byte branch index (0–15).
 *  Entries may appear in any order.  The node hash is always recomputed
 *  from the parsed child hashes (no trusted-hash variant exists for this
 *  format).
 *
 *  @param data Raw wire bytes; must be a multiple of 33 and at most 495 bytes
 *      (15 entries — 16 entries would use the full format instead).
 *  @return A freshly allocated, shareable (cowid = 0) inner node.
 *  @throws std::runtime_error if @p data length is not a multiple of 33,
 *      exceeds 495 bytes, or contains a branch index >= 16.
 */
intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMapInnerNode::makeCompressedInner(Slice data)
{
    // A compressed inner node is serialized as a series of 33 byte chunks,
    // representing a one byte "position" and a 256-bit hash:
    constexpr std::size_t kCHUNK_SIZE = uint256::kBYTES + 1;

    if (auto const s = data.size(); (s % kCHUNK_SIZE != 0) || (s > kCHUNK_SIZE * kBRANCH_FACTOR))
        Throw<std::runtime_error>("Invalid CI node");

    SerialIter si(data);

    auto ret = intr_ptr::makeShared<SHAMapInnerNode>(0, kBRANCH_FACTOR);

    auto hashes = ret->hashesAndChildren_.getHashes();

    while (!si.empty())
    {
        auto const hash = si.getBitString<256>();
        auto const pos = si.get8();

        if (pos >= kBRANCH_FACTOR)
            Throw<std::runtime_error>("invalid CI node");

        hashes[pos].asUInt256() = hash;

        if (hashes[pos].isNonZero())
            ret->isBranch_ |= (1 << pos);
    }

    ret->resizeChildArrays(ret->getBranchCount());
    ret->updateHash();
    return ret;
}

/** Recompute this node's Merkle hash from the current child-hash array.
 *
 *  Feeds HashPrefix::InnerNode followed by all 16 child hashes (zero-filled
 *  for absent branches) into SHA-512/2.  All 16 slots are always consumed
 *  regardless of sparsity, so the hash is layout-independent.  An empty
 *  node (isBranch_ == 0) produces a zero hash.
 *
 *  @note Callers are responsible for ensuring the @c hashes array is up to
 *      date before calling this.  Use updateHashDeep() when child hashes
 *      may have been modified in memory but not yet synced to the array.
 */
void
SHAMapInnerNode::updateHash()
{
    uint256 nh;
    if (isBranch_ != 0)
    {
        sha512_half_hasher h;
        using beast::hash_append;
        hash_append(h, HashPrefix::InnerNode);
        iterChildren([&](SHAMapHash const& hh) { hash_append(h, hh); });
        nh = static_cast<typename sha512_half_hasher::result_type>(h);
    }
    hash_ = SHAMapHash{nh};
}

/** Pull hashes from in-memory children, then recompute this node's hash.
 *
 *  setChild() zeroes the hash slot for a newly installed child so that the
 *  hash is not stale if the child is later modified.  After a batch of
 *  mutations where child hashes have been updated in memory but the local
 *  hashes array was not synchronised, this method propagates those values
 *  upward before delegating to updateHash().
 *
 *  Only non-empty branches that carry a live in-memory pointer are updated;
 *  branches backed only by a known hash (fetched from DB but not yet
 *  instantiated as objects) are left unchanged.
 */
void
SHAMapInnerNode::updateHashDeep()
{
    SHAMapHash* hashes = nullptr;
    intr_ptr::SharedPtr<SHAMapTreeNode>* children = nullptr;
    // structured bindings can't be captured in c++ 17; use tie instead
    std::tie(std::ignore, hashes, children) = hashesAndChildren_.getHashesAndChildren();
    iterNonEmptyChildIndexes([&](auto branchNum, auto indexNum) {
        if (auto p = children[indexNum].get())
            hashes[indexNum] = p->getHash();
    });
    updateHash();
}

/** Serialize this node for peer-to-peer wire transmission.
 *
 *  Chooses between two formats based on occupancy:
 *  - **Compressed** (< 12 branches): emits each non-empty branch as a
 *    32-byte hash followed by a 1-byte position, then appends
 *    kWIRE_TYPE_COMPRESSED_INNER.  Wire size: 33 × n + 1 bytes.
 *  - **Full** (≥ 12 branches): emits all 16 hashes in order, then appends
 *    kWIRE_TYPE_INNER.  Wire size: 513 bytes.
 *
 *  The trailing type byte allows the receiver to select the correct
 *  deserialization factory (makeFullInner or makeCompressedInner).
 *
 *  @param s Serializer to append the encoded bytes to.
 *  @note Asserts that the node is not empty; serializing an empty inner
 *      node is a caller bug.
 */
void
SHAMapInnerNode::serializeForWire(Serializer& s) const
{
    XRPL_ASSERT(!isEmpty(), "xrpl::SHAMapInnerNode::serializeForWire : is non-empty");

    // If the node is sparse, then only send non-empty branches:
    if (getBranchCount() < 12)
    {
        // compressed node
        auto hashes = hashesAndChildren_.getHashes();
        iterNonEmptyChildIndexes([&](auto branchNum, auto indexNum) {
            s.addBitString(hashes[indexNum].asUInt256());
            s.add8(branchNum);
        });
        s.add8(kWIRE_TYPE_COMPRESSED_INNER);
    }
    else
    {
        iterChildren([&](SHAMapHash const& hh) { s.addBitString(hh.asUInt256()); });
        s.add8(kWIRE_TYPE_INNER);
    }
}

/** Serialize this node in canonical hash-input form.
 *
 *  Always emits HashPrefix::InnerNode followed by all 16 child hashes in
 *  branch order (zero-filled for absent branches), matching exactly what
 *  updateHash() feeds to the SHA-512/2 hasher.  Unlike serializeForWire(),
 *  this form is always full (512 + 4 bytes) and carries no wire-type suffix.
 *
 *  @param s Serializer to append the encoded bytes to.
 *  @note Asserts that the node is not empty.
 */
void
SHAMapInnerNode::serializeWithPrefix(Serializer& s) const
{
    XRPL_ASSERT(!isEmpty(), "xrpl::SHAMapInnerNode::serializeWithPrefix : is non-empty");

    s.add32(HashPrefix::InnerNode);
    iterChildren([&](SHAMapHash const& hh) { s.addBitString(hh.asUInt256()); });
}

/** Return a human-readable representation of this node for debugging.
 *
 *  Extends the base-class string with one line per non-empty branch,
 *  formatted as "b<N> = <hash>".
 *
 *  @param id Tree address of this node, forwarded to the base implementation.
 *  @return Multi-line diagnostic string.
 */
std::string
SHAMapInnerNode::getString(SHAMapNodeID const& id) const
{
    std::string ret = SHAMapTreeNode::getString(id);
    auto hashes = hashesAndChildren_.getHashes();
    iterNonEmptyChildIndexes([&](auto branchNum, auto indexNum) {
        ret += "\nb";
        ret += std::to_string(branchNum);
        ret += " = ";
        ret += to_string(hashes[indexNum]);
    });
    return ret;
}

/** Install or remove a child at branch @p m, resizing the arrays if needed.
 *
 *  Computes the destination isBranch_ mask (adding or clearing bit @p m),
 *  then reconstructs hashesAndChildren_ via the TaggedPointer move constructor
 *  that handles simultaneous resize and entry migration.  Installing a child
 *  zeroes its hash slot (marking this node dirty); removing one shrinks the
 *  allocation.  Clears hash_ unconditionally so that the next getHash()
 *  call triggers a recompute.
 *
 *  @param m     Branch index to modify (0–15).
 *  @param child New child pointer; pass nullptr to remove the branch.
 *  @note Requires CoW ownership (cowid_ != 0).  Does not acquire spinlocks —
 *      callers must hold exclusive ownership by construction.
 */
// We are modifying an inner node
void
SHAMapInnerNode::setChild(int m, intr_ptr::SharedPtr<SHAMapTreeNode> child)
{
    XRPL_ASSERT(
        (m >= 0) && (m < kBRANCH_FACTOR), "xrpl::SHAMapInnerNode::setChild : valid branch input");
    XRPL_ASSERT(cowid_, "xrpl::SHAMapInnerNode::setChild : nonzero cowid");
    XRPL_ASSERT(child.get() != this, "xrpl::SHAMapInnerNode::setChild : valid child input");

    auto const dstIsBranch = [&] {
        if (child)
        {
            return isBranch_ | (1u << m);
        }

        return isBranch_ & ~(1u << m);
    }();

    auto const dstToAllocate = popcnt16(dstIsBranch);
    // change hashesAndChildren to remove the element, or make room for the
    // added element, if necessary
    hashesAndChildren_ =
        TaggedPointer(std::move(hashesAndChildren_), isBranch_, dstIsBranch, dstToAllocate);

    isBranch_ = dstIsBranch;

    if (child)
    {
        auto const childIndex =
            *getChildIndex(m);  // NOLINT(bugprone-unchecked-optional-access) isBranch_ set above
        auto [_, hashes, children] = hashesAndChildren_.getHashesAndChildren();
        hashes[childIndex].zero();
        children[childIndex] = std::move(child);
    }

    hash_.zero();

    XRPL_ASSERT(
        getBranchCount() <= hashesAndChildren_.capacity(),
        "xrpl::SHAMapInnerNode::setChild : maximum branch count");
}

/** Store a child pointer into an already-occupied branch without resizing.
 *
 *  Used after the branch has been set (and sized) by setChild(), when the
 *  child object is being made shareable.  Unlike setChild(), this does not
 *  touch isBranch_ or hash_ and does not acquire spinlocks — it is valid
 *  only while the node still has CoW ownership.
 *
 *  @param m     Branch index (0–15); must already be non-empty.
 *  @param child Non-null pointer to the child being shared.
 *  @note Requires CoW ownership (cowid_ != 0) and a pre-existing branch.
 */
// finished modifying, now make shareable
void
SHAMapInnerNode::shareChild(int m, intr_ptr::SharedPtr<SHAMapTreeNode> const& child)
{
    XRPL_ASSERT(
        (m >= 0) && (m < kBRANCH_FACTOR), "xrpl::SHAMapInnerNode::shareChild : valid branch input");
    XRPL_ASSERT(cowid_, "xrpl::SHAMapInnerNode::shareChild : nonzero cowid");
    XRPL_ASSERT(child, "xrpl::SHAMapInnerNode::shareChild : non-null child input");
    XRPL_ASSERT(child.get() != this, "xrpl::SHAMapInnerNode::shareChild : valid child input");

    XRPL_ASSERT(!isEmptyBranch(m), "xrpl::SHAMapInnerNode::shareChild : non-empty branch input");
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) assert above
    hashesAndChildren_.getChildren()[*getChildIndex(m)] = child;
}

/** Return a raw (non-owning) pointer to the child at @p branch.
 *
 *  Acquires the per-child packed spinlock for the child's physical array
 *  index, allowing concurrent access to different children of the same node.
 *  Returns a raw pointer rather than a SharedPtr to avoid bumping the
 *  reference count on hot traversal paths; the caller must not store this
 *  pointer beyond the node's lifetime or release the owning SharedPtr.
 *
 *  @param branch Branch index (0–15); must be non-empty.
 *  @return Raw pointer to the in-memory child, or nullptr if the child has
 *      not yet been loaded into memory (only its hash is known).
 */
SHAMapTreeNode*
SHAMapInnerNode::getChildPointer(int branch)
{
    XRPL_ASSERT(
        branch >= 0 && branch < kBRANCH_FACTOR,
        "xrpl::SHAMapInnerNode::getChildPointer : valid branch input");
    XRPL_ASSERT(
        !isEmptyBranch(branch), "xrpl::SHAMapInnerNode::getChildPointer : non-empty branch input");

    auto const index =
        *getChildIndex(branch);  // NOLINT(bugprone-unchecked-optional-access) assert above

    PackedSpinlock sl(lock_, index);
    std::scoped_lock const lock(sl);
    return hashesAndChildren_.getChildren()[index].get();
}

/** Return a reference-counted pointer to the child at @p branch.
 *
 *  Acquires the per-child packed spinlock to safely copy the SharedPtr while
 *  another thread may be installing a pointer via canonicalizeChild().
 *  Prefer getChildPointer() on hot paths where the caller can ensure the
 *  node outlives the pointer use.
 *
 *  @param branch Branch index (0–15); must be non-empty.
 *  @return SharedPtr to the in-memory child, empty if not yet loaded.
 */
intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMapInnerNode::getChild(int branch)
{
    XRPL_ASSERT(
        branch >= 0 && branch < kBRANCH_FACTOR,
        "xrpl::SHAMapInnerNode::getChild : valid branch input");
    XRPL_ASSERT(!isEmptyBranch(branch), "xrpl::SHAMapInnerNode::getChild : non-empty branch input");

    auto const index =
        *getChildIndex(branch);  // NOLINT(bugprone-unchecked-optional-access) assert above

    PackedSpinlock sl(lock_, index);
    std::scoped_lock const lock(sl);
    return hashesAndChildren_.getChildren()[index];
}

/** Return the known hash for child branch @p m.
 *
 *  For branches present in the tree (isBranch_ bit set), returns a reference
 *  into the internal hashes array.  For absent branches, returns a reference
 *  to the shared zero-hash sentinel kZERO_SHA_MAP_HASH.
 *
 *  @param m Branch index (0–15).
 *  @return Reference to the child hash, or kZERO_SHA_MAP_HASH if empty.
 *  @note The reference is valid for the lifetime of this node.  Do not store
 *      it across mutations of this node.
 */
SHAMapHash const&
SHAMapInnerNode::getChildHash(int m) const
{
    XRPL_ASSERT(
        (m >= 0) && (m < kBRANCH_FACTOR),
        "xrpl::SHAMapInnerNode::getChildHash : valid branch input");
    if (auto const i = getChildIndex(m))
        return hashesAndChildren_.getHashes()[*i];

    return kZERO_SHA_MAP_HASH;
}

/** Deduplicate a freshly loaded child using first-writer-wins under a spinlock.
 *
 *  When multiple threads simultaneously fetch the same child from backing
 *  storage, each constructs its own SHAMapTreeNode.  This method serialises
 *  installation under the per-child packed spinlock: if the slot is already
 *  occupied (another thread won the race), the caller's @p node is discarded
 *  and the incumbent is returned.  If the slot is empty, @p node is installed
 *  and returned.  Either way the caller receives the single canonical in-memory
 *  instance for this child.
 *
 *  @param branch Branch index (0–15); must be non-empty (hash already known).
 *  @param node   Freshly constructed child node whose hash matches the stored
 *      child hash.
 *  @return The canonical in-memory child pointer (may differ from @p node).
 *  @note Asserts that @p node's hash matches the stored child hash before
 *      acquiring the lock.
 */
intr_ptr::SharedPtr<SHAMapTreeNode>
SHAMapInnerNode::canonicalizeChild(int branch, intr_ptr::SharedPtr<SHAMapTreeNode> node)
{
    XRPL_ASSERT(
        branch >= 0 && branch < kBRANCH_FACTOR,
        "xrpl::SHAMapInnerNode::canonicalizeChild : valid branch input");
    XRPL_ASSERT(node != nullptr, "xrpl::SHAMapInnerNode::canonicalizeChild : valid node input");
    XRPL_ASSERT(
        !isEmptyBranch(branch),
        "xrpl::SHAMapInnerNode::canonicalizeChild : non-empty branch input");
    auto const childIndex =
        *getChildIndex(branch);  // NOLINT(bugprone-unchecked-optional-access) assert above
    auto [_, hashes, children] = hashesAndChildren_.getHashesAndChildren();
    XRPL_ASSERT(
        node->getHash() == hashes[childIndex],
        "xrpl::SHAMapInnerNode::canonicalizeChild : node and branch inputs "
        "hash do match");

    PackedSpinlock sl(lock_, childIndex);
    std::scoped_lock const lock(sl);

    if (children[childIndex])
    {
        // There is already a node hooked up, return it
        node = children[childIndex];
    }
    else
    {
        // Hook this node up
        children[childIndex] = node;
    }
    return node;
}

/** Assert internal consistency of this node's state.
 *
 *  Verifies that the isBranch_ bitmask, the hashes array, and the children
 *  array are mutually consistent, and recurses into each in-memory child.
 *  Two layout paths are exercised: sparse (numAllocated < 16) and dense
 *  (numAllocated == 16), each with different index mapping.
 *
 *  Additional checks when @p isRoot is false:
 *  - hash_ must be non-zero.
 *  - At least one non-empty branch must exist.
 *
 *  @param isRoot Set to true when checking the trie root, which is permitted
 *      to have a zero hash while empty.
 *  @note Called during testing and debugging; not compiled out in release.
 */
void
SHAMapInnerNode::invariants(bool isRoot) const
{
    [[maybe_unused]] unsigned count = 0;
    auto [numAllocated, hashes, children] = hashesAndChildren_.getHashesAndChildren();

    if (numAllocated != kBRANCH_FACTOR)
    {
        auto const branchCount = getBranchCount();
        for (int i = 0; i < branchCount; ++i)
        {
            XRPL_ASSERT(
                hashes[i].isNonZero(),
                "xrpl::SHAMapInnerNode::invariants : nonzero hash in branch");
            if (children[i] != nullptr)
                children[i]->invariants();
            ++count;
        }
    }
    else
    {
        for (int i = 0; i < kBRANCH_FACTOR; ++i)
        {
            if (hashes[i].isNonZero())
            {
                XRPL_ASSERT(
                    (isBranch_ & (1 << i)),
                    "xrpl::SHAMapInnerNode::invariants : valid branch when "
                    "nonzero hash");
                if (children[i] != nullptr)
                    children[i]->invariants();
                ++count;
            }
            else
            {
                XRPL_ASSERT(
                    (isBranch_ & (1 << i)) == 0,
                    "xrpl::SHAMapInnerNode::invariants : valid branch when "
                    "zero hash");
            }
        }
    }

    if (!isRoot)
    {
        XRPL_ASSERT(hash_.isNonZero(), "xrpl::SHAMapInnerNode::invariants : nonzero hash");
        XRPL_ASSERT(count >= 1, "xrpl::SHAMapInnerNode::invariants : minimum count");
    }
    XRPL_ASSERT(
        (count == 0) ? hash_.isZero() : hash_.isNonZero(),
        "xrpl::SHAMapInnerNode::invariants : hash and count do match");
}

}  // namespace xrpl
