/** @file
 *  Sparse child-array manager for SHAMap inner nodes.
 *
 *  Defines `TaggedPointer`, which packs a size-class tag into the two low bits
 *  of a heap pointer to manage four pool-backed capacity tiers (2, 4, 6, 16
 *  slots). Also defines `popcnt16`, the popcount primitive used to translate
 *  branch numbers to sparse array indices.
 */

#pragma once

#include <xrpl/basics/IntrusivePointer.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <array>
#include <cstdint>
#include <optional>

namespace xrpl {

/** Sparse co-located array pair for `SHAMapInnerNode` children.
 *
 *  Owns a single heap allocation that holds a `SHAMapHash[]` immediately
 *  followed by a `SharedPtr<SHAMapTreeNode>[]`, both of the same length N.
 *  N is one of four capacity tiers (2, 4, 6, or 16) chosen by rounding the
 *  requested child count up to the nearest boundary. The tier index (0–3) is
 *  stored in the two low bits of the allocation pointer, which are always zero
 *  due to `SHAMapHash`'s minimum alignment of 4.
 *
 *  In the **dense** tier (N == 16, tag == 3) the logical branch number is also
 *  the array index. In **sparse** tiers only occupied children are stored,
 *  packed in ascending branch-index order; callers translate via
 *  `getChildIndex()` (a single `popcnt16` on the occupancy bitset). The
 *  occupancy bitset itself (`isBranch_`) lives in `SHAMapInnerNode`, not here.
 *
 *  Each tier is backed by its own `boost::singleton_pool` (512 KB blocks),
 *  keeping allocation O(1) and avoiding general-purpose heap fragmentation for
 *  these hot, fixed-size objects. On a typical production ledger this layout
 *  reduces inner-node memory to roughly 25% of a fully-dense allocation.
 *
 *  `TaggedPointer` is move-only. A moved-from instance has `tp_ == 0`, which
 *  `destroyHashesAndChildren()` treats as a no-op sentinel.
 *
 *  @note The `boundaries` array in `TaggedPointer.ipp` must have exactly 4
 *      entries; a `static_assert` enforces this because the tag field is only
 *      2 bits wide.
 *  @see SHAMapInnerNode for the owning class and its `isBranch_` occupancy
 *      bitset.
 *  @see popcnt16 for the branch-number-to-array-index primitive.
 */
class TaggedPointer
{
private:
    static_assert(
        alignof(SHAMapHash) >= 4,
        "Bad alignment: Tag pointer requires low two bits to be zero.");

    /** Combined pointer and 2-bit size-class tag.
     *
     *  High bits (`& kPTR_MASK`) are the raw allocation address; low 2 bits
     *  (`& kTAG_MASK`) index into the `boundaries` array to give the array
     *  capacity. Set to 0 in a moved-from instance.
     */
    std::uintptr_t tp_ = 0;

    /** Mask to extract the 2-bit size-class tag from `tp_`. */
    static constexpr std::uintptr_t kTAG_MASK = 3;

    /** Mask to extract the raw pointer from `tp_` (clears the tag bits). */
    static constexpr std::uintptr_t kPTR_MASK = ~kTAG_MASK;

    /** Run element destructors on all allocated slots and return memory to the
     *  pool. A no-op when `tp_ == 0` (moved-from or default state).
     */
    void
    destroyHashesAndChildren();

    /** Tag type used to select the raw-allocate constructor overload.
     *
     *  An empty struct whose sole purpose is to make the intent of the
     *  private constructor explicit at each call site: the caller is taking
     *  responsibility for running placement-new on every allocated slot before
     *  any destructor can fire.
     */
    struct RawAllocateTag
    {
    };

    /** Allocate pool memory for at least `numChildren` slots without running
     *  element constructors.
     *
     *  The actual capacity is rounded up to the nearest tier boundary. Because
     *  the destructor unconditionally calls `destroyHashesAndChildren()`, which
     *  runs destructors on all allocated slots, every call site **must**
     *  follow this constructor with placement-new loops covering all allocated
     *  slots before any code path that could throw or early-return.
     *
     *  @param RawAllocateTag Overload selector; pass `RawAllocateTag{}`.
     *  @param numChildren Minimum number of slots to allocate; must be ≤
     *      `SHAMapInnerNode::kBRANCH_FACTOR`.
     */
    explicit TaggedPointer(RawAllocateTag, std::uint8_t numChildren);

public:
    TaggedPointer() = delete;

    /** Allocate and default-construct arrays for at least `numChildren` slots.
     *
     *  Rounds `numChildren` up to the nearest tier boundary, allocates from
     *  the corresponding pool, and runs default constructors on every
     *  `SHAMapHash` and `SharedPtr<SHAMapTreeNode>` slot.
     *
     *  @param numChildren Minimum capacity; must be ≤
     *      `SHAMapInnerNode::kBRANCH_FACTOR`.
     */
    explicit TaggedPointer(std::uint8_t numChildren);

    /** Resize the allocation, preserving existing children from `other`.
     *
     *  If `toAllocate` maps to the same tier as `other`'s current capacity
     *  the allocation is reused in-place; otherwise a new pool block is
     *  allocated and all non-empty children (identified by `isBranch`) are
     *  moved into it. Remaining slots are default-constructed.
     *
     *  Implemented as a constructor (rather than a member function) to avoid
     *  unnecessary copies and zero-fills that would occur if the resize were
     *  performed inside `SHAMapInnerNode` directly.
     *
     *  @param other Source `TaggedPointer`; left in a valid moved-from state.
     *  @param isBranch Occupancy bitset for `other`: bit `i` set means branch
     *      `i` is non-empty.
     *  @param toAllocate Minimum capacity for the result; must be ≥
     *      `popcnt16(isBranch)` and ≤ `SHAMapInnerNode::kBRANCH_FACTOR`.
     */
    explicit TaggedPointer(TaggedPointer&& other, std::uint16_t isBranch, std::uint8_t toAllocate);

    /** Resize the allocation and simultaneously apply a branch-set delta.
     *
     *  Constructs a new `TaggedPointer` whose logical children are the
     *  intersection of `other`'s children and `dstBranches`, with empty slots
     *  prepared for any branch in `dstBranches` that was absent in
     *  `srcBranches`. When the old and new tier are identical the operation is
     *  performed in-place (shift left to remove, shift right to insert);
     *  otherwise a fresh pool block is allocated and elements are copied across
     *  with placement-new.
     *
     *  Used by `SHAMapInnerNode::resizeChildArrays()` when an add or remove
     *  operation changes the occupied count across a tier boundary.
     *
     *  @param other Source `TaggedPointer`; left in a valid moved-from state.
     *  @param srcBranches Occupancy bitset of non-empty children in `other`.
     *  @param dstBranches Occupancy bitset for the result. A bit may be set in
     *      `dstBranches` but absent in `srcBranches` — in a sparse result an
     *      empty slot is reserved at the correct sorted position for the new
     *      child. `srcBranches` and `dstBranches` typically differ by one bit.
     *  @param toAllocate Minimum capacity for the result; must be ≥
     *      `popcnt16(dstBranches)` and ≤ `SHAMapInnerNode::kBRANCH_FACTOR`.
     *  @note If the two bitsets differ by more than one bit the function
     *      remains correct but may not be the most efficient approach for that
     *      use-case.
     */
    explicit TaggedPointer(
        TaggedPointer&& other,
        std::uint16_t srcBranches,
        std::uint16_t dstBranches,
        std::uint8_t toAllocate);

    TaggedPointer(TaggedPointer const&) = delete;

    /** Move constructor. Transfers ownership; leaves `other` in a moved-from
     *  state (`tp_ == 0`).
     */
    TaggedPointer(TaggedPointer&&);

    /** Move-assignment operator. Destroys the current allocation, then
     *  transfers ownership from `other`, leaving `other` moved-from.
     */
    TaggedPointer&
    operator=(TaggedPointer&&);

    /** Destroy all allocated slots and return memory to the pool. */
    ~TaggedPointer();

    /** Separate the stored pointer from its 2-bit size-class tag.
     *
     *  @return A pair of `{tag, rawPtr}` where `tag` is the 2-bit tier index
     *      into `boundaries` and `rawPtr` is the untagged allocation address.
     */
    [[nodiscard]] std::pair<std::uint8_t, void*>
    decode() const;

    /** Number of slots allocated in each array (hashes and children).
     *
     *  This is `boundaries[tag]` — one of 2, 4, 6, or 16.
     *
     *  @return The array capacity for the current size-class tier.
     */
    [[nodiscard]] std::uint8_t
    capacity() const;

    /** Return true when the allocation holds all 16 (`kBRANCH_FACTOR`) slots.
     *
     *  In the dense layout branch number equals array index directly, so no
     *  popcount translation is needed.
     *
     *  @return `true` if the tag equals the last (dense) tier index.
     */
    [[nodiscard]] bool
    isDense() const;

    /** Return the array capacity and raw pointers to both co-located arrays.
     *
     *  The `SHAMapHash` array begins at the allocation base; the
     *  `SharedPtr<SHAMapTreeNode>` array begins immediately after (at
     *  `hashes + numAllocated`). Both arrays have `numAllocated` elements.
     *
     *  @return A tuple of `{numAllocated, hashes, children}`.
     */
    [[nodiscard]] std::tuple<std::uint8_t, SHAMapHash*, intr_ptr::SharedPtr<SHAMapTreeNode>*>
    getHashesAndChildren() const;

    /** Return a pointer to the start of the `SHAMapHash` array.
     *
     *  Equivalent to `std::get<1>(getHashesAndChildren())` but slightly
     *  cheaper when the children pointer is not needed.
     *
     *  @return Pointer to the first `SHAMapHash` element.
     */
    [[nodiscard]] SHAMapHash*
    getHashes() const;

    /** Return a pointer to the start of the `SharedPtr<SHAMapTreeNode>` array.
     *
     *  @return Pointer to the first child smart-pointer element.
     */
    [[nodiscard]] intr_ptr::SharedPtr<SHAMapTreeNode>*
    getChildren() const;

    /** Invoke `f` for all 16 branches, supplying each branch's hash.
     *
     *  Empty branches in a sparse layout receive a zero-valued `SHAMapHash`.
     *  Iterates all `kBRANCH_FACTOR` branches in ascending branch-index order,
     *  making this suitable for `updateHash()` which must feed all 16 hashes
     *  to the SHA-512 half-hash regardless of occupancy.
     *
     *  @tparam F Callable with signature `void(SHAMapHash const&)`.
     *  @param isBranch Occupancy bitset; bit `i` set means branch `i` is
     *      non-empty and its hash is stored in the array.
     *  @param f Callback invoked once per branch in branch-index order.
     */
    template <class F>
    void
    iterChildren(std::uint16_t isBranch, F&& f) const;

    /** Invoke `f` for every non-empty branch with both its branch number and
     *  its physical array index.
     *
     *  For a dense layout the two values are identical. For a sparse layout
     *  the array index is the packed position (`popcnt16` of lower set bits in
     *  `isBranch`), which differs from the branch number whenever any
     *  lower-numbered branch is absent. Callers that need to index into the
     *  hashes or children arrays (e.g., mutation helpers) must use the
     *  array index, not the branch number.
     *
     *  @tparam F Callable with signature `void(int branchNum, int arrayIdx)`.
     *  @param isBranch Occupancy bitset of non-empty children.
     *  @param f Callback invoked once per occupied branch in ascending order.
     */
    template <class F>
    void
    iterNonEmptyChildIndexes(std::uint16_t isBranch, F&& f) const;

    /** Translate a logical branch number to a physical array index.
     *
     *  In the dense layout returns `i` directly. In a sparse layout, counts
     *  the number of occupied branches below `i` via `popcnt16`, which gives
     *  the packed array position of branch `i`.
     *
     *  @param isBranch Occupancy bitset of non-empty children.
     *  @param i Logical branch number (0–15) to look up.
     *  @return The array index, or `std::nullopt` if the arrays are sparse and
     *      branch `i` is not occupied (empty branches have no array slot in
     *      sparse mode).
     */
    [[nodiscard]] std::optional<int>
    getChildIndex(std::uint16_t isBranch, int i) const;
};

/** Count the number of set bits in a 16-bit value.
 *
 *  Used by `TaggedPointer::getChildIndex()` to translate a logical branch
 *  number to a sparse array index (number of occupied branches below position
 *  `i`), and by `SHAMapInnerNode::getBranchCount()`. Both are on hot traversal
 *  paths, so the implementation dispatches to the fastest available intrinsic:
 *  `std::popcount` (C++20), `__builtin_popcount` (GCC/Clang), or a
 *  compile-time-generated 256-entry lookup table as a portable fallback.
 *
 *  @param a 16-bit value whose set bits are to be counted.
 *  @return Number of bits set in `a`, in the range [0, 16].
 */
[[nodiscard]] inline int
popcnt16(std::uint16_t a)
{
#if __cpp_lib_bitops
    return std::popcount(a);
#elif defined(__clang__) || defined(__GNUC__)
    return __builtin_popcount(a);
#else
    // fallback to table lookup
    static auto constexpr const tbl = []() {
        std::array<std::uint8_t, 256> ret{};
        for (int i = 0; i != 256; ++i)
        {
            for (int j = 0; j != 8; ++j)
            {
                if (i & (1 << j))
                    ret[i]++;
            }
        }
        return ret;
    }();
    return tbl[a & 0xff] + tbl[a >> 8];
#endif
}

}  // namespace xrpl
