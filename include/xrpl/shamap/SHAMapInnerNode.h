#pragma once

#include <xrpl/basics/IntrusivePointer.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/detail/TaggedPointer.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

namespace xrpl {

/** Branching (non-leaf) node of the SHAMap authenticated Merkle radix tree.
 *
 *  Each inner node fans out into exactly `kBRANCH_FACTOR` (16) children, one
 *  per hexadecimal nibble of a 256-bit key. Together with `SHAMapLeafNode`,
 *  inner nodes form a trie of depth at most 64 levels.
 *
 *  Child hashes and child pointers are stored together in a single sparse
 *  allocation via `TaggedPointer hashesAndChildren_`. When fewer than all 16
 *  branches are occupied the arrays are kept compact (packed in branch-index
 *  order), reducing per-node memory to roughly 25% of a dense layout for a
 *  typical production tree.
 *
 *  The `lock_` field is a 16-bit atomic bitlock — one bit per child slot —
 *  allowing concurrent access to *different* children of the same node without
 *  global serialization.
 *
 *  @note Callers must `clone()` a shared node (one with `cowid() == 0`) before
 *      mutating it. `setChild()` and `shareChild()` both assert `cowid_ != 0`.
 *  @see SHAMapTreeNode for copy-on-write ownership semantics.
 *  @see TaggedPointer for the sparse storage implementation.
 */
class SHAMapInnerNode final : public SHAMapTreeNode, public CountedObject<SHAMapInnerNode>
{
public:
    /** Number of children per inner node — one per hex nibble of the key. */
    static constexpr unsigned int kBRANCH_FACTOR = 16;

private:
    /** Co-located sparse arrays: `SHAMapHash[N]` followed by
     *  `SharedPtr<SHAMapTreeNode>[N]`, where N is determined by the 2-bit tag
     *  stored in the pointer's low bits (capacity tiers: 2, 4, 8, 16).
     *
     *  `isBranch_` is the authoritative occupancy bitset; in sparse mode the
     *  arrays hold only non-empty children packed in ascending branch-index
     *  order.
     */
    TaggedPointer hashesAndChildren_;

    /** Generation counter used by the full-below optimization.
     *
     *  When equal to the current sync generation, every node in the subtree
     *  below this node is known to be present locally and does not need to be
     *  fetched from peers.
     */
    std::uint32_t fullBelowGen_ = 0;

    /** Bitmask of occupied branches; bit `i` is set iff branch `i` is
     *  non-empty. This is the single source of truth for occupancy; the
     *  `hashesAndChildren_` arrays are sized accordingly.
     */
    std::uint16_t isBranch_ = 0;

    /** Per-child bit spinlock, one bit per physical array slot.
     *
     *  Allows concurrent reads of *different* children without a global lock.
     *  `getChild()`, `getChildPointer()`, and `canonicalizeChild()` all
     *  acquire only the single bit corresponding to the target child's array
     *  index. `setChild()` and `shareChild()` skip locking because they
     *  require CoW ownership (asserted via `cowid_`).
     */
    mutable std::atomic<std::uint16_t> lock_ = 0;

    /** Resize the co-located hash and child-pointer arrays to accommodate
     *  at least `toAllocate` children.
     *
     *  Existing children are preserved; the caller is responsible for
     *  updating `isBranch_` before and after as needed. Because
     *  `TaggedPointer` only supports four capacity tiers (2, 4, 8, 16), the
     *  actual allocation may be larger than `toAllocate`.
     *
     *  @param toAllocate minimum required capacity; must be ≤ `kBRANCH_FACTOR`.
     */
    void
    resizeChildArrays(std::uint8_t toAllocate);

    /** Translate a logical branch number to a physical array index.
     *
     *  In sparse mode, only occupied branches are stored, so the physical index
     *  is `popcount(isBranch_ & ((1 << i) - 1))`. In dense mode (all 16 slots
     *  allocated) branch number equals array index.
     *
     *  @param i Logical branch number (0–15).
     *  @return The physical array index, or `std::nullopt` if the branch is
     *      empty and the arrays are in sparse mode.
     */
    std::optional<int>
    getChildIndex(int i) const;

    /** Invoke `f` for every one of the 16 branches, passing each branch's
     *  `SHAMapHash` — zero-hash for empty branches.
     *
     *  Used by `updateHash()` and `serializeForWire()` to iterate the full
     *  logical child-hash array without caring about sparse vs. dense layout.
     *
     *  @tparam F Callable with signature `void(SHAMapHash const&)`.
     *  @param f Callback invoked once per branch in branch-index order.
     */
    template <class F>
    void
    iterChildren(F&& f) const;

    /** Invoke `f` for every non-empty branch, passing both the logical branch
     *  number and the physical array index.
     *
     *  For a dense (16-element) layout the two values are identical. For a
     *  sparse layout the array index is the packed position, which differs
     *  from the branch number when lower-numbered branches are absent.
     *
     *  @tparam F Callable with signature `void(int branchNum, int arrayIdx)`.
     *  @param f Callback invoked once per occupied branch.
     */
    template <class F>
    void
    iterNonEmptyChildIndexes(F&& f) const;

public:
    /** Construct an inner node owned by the given SHAMap copy-on-write epoch.
     *
     *  @param cowid Copy-on-write identifier of the owning SHAMap; pass 0 for
     *      a shareable (read-only) node.
     *  @param numAllocatedChildren Initial array capacity; defaults to 2
     *      (smallest sparse tier). The actual allocation may be rounded up to
     *      the next supported tier.
     */
    explicit SHAMapInnerNode(std::uint32_t cowid, std::uint8_t numAllocatedChildren = 2);

    SHAMapInnerNode(SHAMapInnerNode const&) = delete;
    SHAMapInnerNode&
    operator=(SHAMapInnerNode const&) = delete;
    ~SHAMapInnerNode() override;

    /** Release all child `SharedPtr`s before the node's memory is reclaimed.
     *
     *  Called by the intrusive reference-count infrastructure when the strong
     *  count reaches zero but weak references may still exist. Explicitly
     *  resets every occupied child slot so that downstream reference counts are
     *  decremented promptly, breaking potential reference cycles even while
     *  the storage itself outlives its strong references.
     */
    void
    partialDestructor() override;

    /** Produce an independent copy of this node assigned to a new CoW epoch.
     *
     *  Allocates a new `SHAMapInnerNode` with `cowid`, copies all hashes and
     *  child pointers, and right-sizes the sparse arrays to actual occupancy.
     *  Hashes are copied without locking (they are immutable on shared nodes);
     *  child pointers are copied under `lock_` to prevent races with
     *  concurrent `canonicalizeChild()` calls.
     *
     *  @param cowid Copy-on-write identifier for the new node's owning map.
     *  @return A fully independent `SharedPtr` to the cloned node.
     */
    intr_ptr::SharedPtr<SHAMapTreeNode>
    clone(std::uint32_t cowid) const override;

    /** @return `SHAMapNodeType::TnInner`. */
    SHAMapNodeType
    getType() const override
    {
        return SHAMapNodeType::TnInner;
    }

    /** @return `false` — inner nodes are never leaves. */
    bool
    isLeaf() const override
    {
        return false;
    }

    /** @return `true` — this is always an inner node. */
    bool
    isInner() const override
    {
        return true;
    }

    /** @return `true` if no branches are populated (`isBranch_ == 0`). */
    bool
    isEmpty() const;

    /** Check whether a specific branch is unoccupied.
     *
     *  @param m Branch index (0–15).
     *  @return `true` if branch `m` has no child.
     */
    bool
    isEmptyBranch(int m) const;

    /** @return The number of populated branches (popcount of `isBranch_`). */
    int
    getBranchCount() const;

    /** Return the Merkle hash committed to branch `m`.
     *
     *  @param m Branch index (0–15).
     *  @return The child's `SHAMapHash`, or the zero hash if the branch is
     *      empty.
     */
    SHAMapHash const&
    getChildHash(int m) const;

    /** Replace the child at branch `m`, resizing the sparse arrays as needed.
     *
     *  Passing a null `child` removes the branch; passing a non-null pointer
     *  installs it. Zeroes the corresponding hash entry so that the next
     *  `updateHash()` recomputes from the pointer. Zeroes `hash_` to mark
     *  this node dirty.
     *
     *  @param m Branch index (0–15).
     *  @param child New child node, or null to clear the branch.
     *  @note Asserts `cowid_ != 0` — the node must be CoW-owned before
     *      mutation. Asserts `child.get() != this` to prevent self-loops.
     */
    void
    setChild(int m, intr_ptr::SharedPtr<SHAMapTreeNode> child);

    /** Install a child pointer into an already-occupied branch without
     *  resizing arrays or dirtying the hash.
     *
     *  Used during tree construction when the branch is known to exist (e.g.,
     *  after `setChild` allocated the slot). Unlike `setChild`, this does not
     *  zero `hash_` and does not resize the arrays.
     *
     *  @param m Branch index (0–15); must already be non-empty.
     *  @param child Non-null child pointer to install.
     *  @note Asserts `cowid_ != 0`, `child != null`, and that branch `m` is
     *      already occupied.
     */
    void
    shareChild(int m, intr_ptr::SharedPtr<SHAMapTreeNode> const& child);

    /** Return a raw (non-owning) pointer to the child at branch `m`.
     *
     *  Acquires the per-child bit spinlock for `m`'s physical array index,
     *  then reads the pointer. The returned pointer is only valid while the
     *  caller holds the tree in a state that prevents the node from being
     *  released.
     *
     *  @param branch Branch index (0–15); must be non-empty.
     *  @return Raw pointer to the child node (never null for a non-empty branch
     *      that has been loaded from storage).
     */
    SHAMapTreeNode*
    getChildPointer(int branch);

    /** Return a ref-counted pointer to the child at branch `m`.
     *
     *  Acquires the per-child bit spinlock for the physical array index before
     *  copying the `SharedPtr`, ensuring the reference count is incremented
     *  atomically with respect to concurrent `canonicalizeChild()` calls.
     *
     *  @param branch Branch index (0–15); must be non-empty.
     *  @return `SharedPtr` to the child node.
     */
    intr_ptr::SharedPtr<SHAMapTreeNode>
    getChild(int branch);

    /** Deduplicate a concurrently loaded child node.
     *
     *  When multiple threads fetch the same child from backing storage
     *  simultaneously, this method serializes installation under the per-child
     *  bit spinlock. The first caller installs `node` and gets it back; any
     *  subsequent caller discards its freshly-deserialized copy and receives
     *  the incumbent pointer instead ("winner keeps it"). The supplied node's
     *  hash must match the stored branch hash.
     *
     *  @param branch Branch index (0–15); must be non-empty.
     *  @param node Freshly-loaded node whose hash equals `getChildHash(branch)`.
     *  @return The canonical (winning) pointer for this child slot — either
     *      `node` itself (if this caller won) or the pre-existing pointer.
     *  @note Asserts that `node->getHash() == getChildHash(branch)`.
     */
    intr_ptr::SharedPtr<SHAMapTreeNode>
    canonicalizeChild(int branch, intr_ptr::SharedPtr<SHAMapTreeNode> node);

    /** Check whether the entire subtree below this node is locally complete.
     *
     *  Returns `true` when `fullBelowGen_` equals `generation`, meaning a
     *  previous sync pass confirmed all descendant nodes are present in local
     *  storage. Because generations are monotonically increasing, a stale
     *  marker automatically becomes invalid on the next sync cycle.
     *
     *  @param generation Current sync generation from `FullBelowCache`.
     *  @return `true` if the subtree is known to be complete for `generation`.
     */
    bool
    isFullBelow(std::uint32_t generation) const;

    /** Mark the entire subtree below this node as locally complete.
     *
     *  Records `gen` in `fullBelowGen_`. Subsequent calls to
     *  `isFullBelow(gen)` will return `true`, allowing traversal to skip this
     *  subtree when scanning for missing nodes.
     *
     *  @param gen Current sync generation to record.
     */
    void
    setFullBelowGen(std::uint32_t gen);

    /** Recompute `hash_` as SHA-512/2 of `HashPrefix::InnerNode` followed by
     *  all 16 child hashes (zero-hashes for empty branches).
     *
     *  Reads hashes from the local `hashesAndChildren_` arrays; does NOT pull
     *  hashes from in-memory child objects. Call `updateHashDeep()` instead
     *  when child nodes were set via pointer without updating the hash arrays.
     */
    void
    updateHash() override;

    /** Sync child hashes from in-memory child objects then recompute this
     *  node's hash.
     *
     *  For every occupied branch that has a non-null child pointer, copies
     *  `child->getHash()` into the local hash array, then delegates to
     *  `updateHash()`. Needed after batch mutations where child nodes were
     *  installed via pointer but the corresponding hash slots were not updated.
     */
    void
    updateHashDeep();

    /** Serialize this node for peer-to-peer wire transmission.
     *
     *  Chooses format based on occupancy:
     *  - Fewer than 12 populated branches: *compressed inner* format — each
     *    non-empty branch is emitted as 32 bytes of hash followed by 1 byte of
     *    branch index (33 bytes per child), terminated by
     *    `kWIRE_TYPE_COMPRESSED_INNER`.
     *  - 12 or more branches: *full inner* format — all 16 hashes in order
     *    (512 bytes), terminated by `kWIRE_TYPE_INNER`.
     *
     *  @param s Serializer to append to.
     *  @note Asserts that the node is non-empty before serializing.
     */
    void
    serializeForWire(Serializer&) const override;

    /** Serialize this node in canonical hash-input form.
     *
     *  Prepends `HashPrefix::InnerNode` (4 bytes) then emits all 16 child
     *  hashes in order (512 bytes), regardless of actual occupancy. This is
     *  the form consumed by `updateHash()` and verified by Merkle proof
     *  checks.
     *
     *  @param s Serializer to append to.
     *  @note Asserts that the node is non-empty before serializing.
     */
    void
    serializeWithPrefix(Serializer&) const override;

    /** Build a human-readable description for debugging.
     *
     *  Appends each non-empty branch number and its hash to the base-class
     *  string from `SHAMapTreeNode::getString`.
     *
     *  @param id The tree address of this node, used by the base-class portion.
     *  @return Multiline string with one `bN = <hash>` line per occupied branch.
     */
    std::string
    getString(SHAMapNodeID const&) const override;

    /** Verify structural invariants in debug builds.
     *
     *  Checks that every occupied branch has a non-zero hash, that `isBranch_`
     *  and the hash array agree on occupancy, and (unless this is the root)
     *  that `hash_` is non-zero and at least one branch is occupied.
     *
     *  @param isRoot Pass `true` when checking the tree root; relaxes the
     *      non-zero-hash and minimum-count assertions that do not apply to an
     *      empty root.
     */
    void
    invariants(bool isRoot = false) const override;

    /** Deserialize an inner node from the *full inner* wire format.
     *
     *  Expects exactly `kBRANCH_FACTOR * 32` bytes: 16 consecutive 256-bit
     *  hashes. After parsing, right-sizes the sparse arrays to actual
     *  occupancy via `resizeChildArrays()`.
     *
     *  @param data Raw bytes in full-inner format.
     *  @param hash Expected Merkle hash of this node.
     *  @param hashValid If `true`, assign `hash` directly; if `false`,
     *      recompute via `updateHash()`.
     *  @return A `SharedPtr<SHAMapTreeNode>` to the new inner node.
     *  @throws std::runtime_error if `data.size()` is not exactly 512 bytes.
     */
    static intr_ptr::SharedPtr<SHAMapTreeNode>
    makeFullInner(Slice data, SHAMapHash const& hash, bool hashValid);

    /** Deserialize an inner node from the *compressed inner* wire format.
     *
     *  Expects a sequence of 33-byte chunks: 32 bytes of hash followed by
     *  1 byte of branch index. After parsing, right-sizes the sparse arrays
     *  and recomputes the hash via `updateHash()`.
     *
     *  @param data Raw bytes in compressed-inner format; size must be a
     *      non-zero multiple of 33 and at most `33 * kBRANCH_FACTOR`.
     *  @return A `SharedPtr<SHAMapTreeNode>` to the new inner node.
     *  @throws std::runtime_error if the size is invalid or a branch index
     *      is ≥ `kBRANCH_FACTOR`.
     */
    static intr_ptr::SharedPtr<SHAMapTreeNode>
    makeCompressedInner(Slice data);
};

inline bool
SHAMapInnerNode::isEmpty() const
{
    return isBranch_ == 0;
}

inline bool
SHAMapInnerNode::isEmptyBranch(int m) const
{
    return (isBranch_ & (1 << m)) == 0;
}

inline int
SHAMapInnerNode::getBranchCount() const
{
    return popcnt16(isBranch_);
}

inline bool
SHAMapInnerNode::isFullBelow(std::uint32_t generation) const
{
    return fullBelowGen_ == generation;
}

inline void
SHAMapInnerNode::setFullBelowGen(std::uint32_t gen)
{
    fullBelowGen_ = gen;
}

}  // namespace xrpl
