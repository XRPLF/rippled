/** @file
 *  Defines `SHAMapItem`, the immutable, slab-allocated payload leaf of the
 *  XRP Ledger's SHAMap Merkle-Patricia trie, together with its slab allocator
 *  pool (`detail::gSlabber`), `boost::intrusive_ptr` lifetime hooks, and the
 *  `makeShamapitem` factory functions.
 */

#pragma once

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/SlabAllocator.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <boost/smart_ptr/intrusive_ptr.hpp>

namespace xrpl {

/** Immutable, slab-allocated payload item stored at the leaves of a SHAMap.
 *
 *  Each `SHAMapItem` pairs a 256-bit trie key (`tag_`) with an opaque
 *  variable-length byte payload. The payload is stored via the struct-hack:
 *  it occupies the memory immediately after the fixed-size struct fields in
 *  the same allocation, avoiding a second heap allocation and keeping the
 *  header and payload in one contiguous block.
 *
 *  Objects are always managed through `boost::intrusive_ptr<SHAMapItem const>`
 *  using an embedded atomic reference count. Once constructed the key and
 *  payload are immutable; the SHAMap copy-on-write protocol creates a new
 *  item rather than mutating an existing one, allowing the same item to be
 *  shared across CoW snapshots safely.
 *
 *  The only valid construction path is `makeShamapitem()`. The default,
 *  copy, and move constructors are all deleted. `CountedObject<SHAMapItem>`
 *  maintains a global live-object counter for diagnostics.
 *
 *  @note Payload size is limited to 16 MiB (asserted in `makeShamapitem`).
 *      The struct must have `alignof` of 4 or 8 (enforced by `static_assert`)
 *      to satisfy the slab allocator's alignment contract.
 */
class SHAMapItem : public CountedObject<SHAMapItem>
{
    friend void
    intrusive_ptr_add_ref(SHAMapItem const* x);

    friend void
    intrusive_ptr_release(SHAMapItem const* x);

    friend boost::intrusive_ptr<SHAMapItem>
    makeShamapitem(uint256 const& tag, Slice data);

private:
    uint256 const tag_;

    // We use std::uint32_t to minimize the size; there's no SHAMapItem whose
    // size exceeds 4GB and there won't ever be (famous last words?), so this
    // is safe.
    std::uint32_t const size_;

    // Embedded reference count for boost::intrusive_ptr. Initialised to 1 so
    // makeShamapitem can pass `false` to suppress the automatic increment that
    // would otherwise bring the count to 2.
    mutable std::atomic<std::uint32_t> refcount_ = 1;

    /** Placement-new constructor; must only be called by `makeShamapitem`.
     *
     *  Copies `data` into the memory immediately following the struct fields.
     *  The caller is responsible for pre-allocating `sizeof(SHAMapItem) +
     *  data.size()` bytes before invoking this via placement new.
     */
    SHAMapItem(uint256 const& tag, Slice data)
        : tag_(tag), size_(static_cast<std::uint32_t>(data.size()))
    {
        std::memcpy(
            reinterpret_cast<std::uint8_t*>(this) + sizeof(*this), data.data(), data.size());
    }

public:
    SHAMapItem() = delete;

    SHAMapItem(SHAMapItem const& other) = delete;

    SHAMapItem&
    operator=(SHAMapItem const& other) = delete;

    SHAMapItem(SHAMapItem&& other) = delete;

    SHAMapItem&
    operator=(SHAMapItem&&) = delete;

    /** Return the 256-bit trie key that identifies this item in the SHAMap. */
    uint256 const&
    key() const
    {
        return tag_;
    }

    /** Return the size of the payload in bytes. */
    std::size_t
    size() const
    {
        return size_;
    }

    /** Return a pointer to the first byte of the payload.
     *
     *  The payload is stored immediately after the struct in the same
     *  allocation (struct-hack layout). The pointer is valid for the
     *  lifetime of this object.
     */
    void const*
    data() const
    {
        return reinterpret_cast<std::uint8_t const*>(this) + sizeof(*this);
    }

    /** Return the payload as a `Slice` (pointer + length view). */
    Slice
    slice() const
    {
        return {data(), size()};
    }
};

namespace detail {

// clang-format off
/** Slab allocator pool backing all `SHAMapItem` allocations.
 *
 *  Seven size tiers cover payloads of up to 1052 extra bytes (added to
 *  `sizeof(SHAMapItem)`). Tier cutoffs and pool sizes are tuned to the
 *  empirical distribution of ledger-object sizes and minimise intra-block
 *  padding. Each backing block is allocated at a 2 MiB boundary to allow
 *  Linux transparent huge-page (THP) support to engage automatically.
 *
 *  Payloads exceeding the largest tier are served by a plain
 *  `new uint8_t[]` fallback in `makeShamapitem`.
 */
inline SlabAllocatorSet<SHAMapItem> gSlabber({
    {  128, megabytes(std::size_t(60)) },
    {  192, megabytes(std::size_t(46)) },
    {  272, megabytes(std::size_t(60)) },
    {  384, megabytes(std::size_t(56)) },
    {  564, megabytes(std::size_t(40)) },
    {  772, megabytes(std::size_t(46)) },
    { 1052, megabytes(std::size_t(60)) },
});
// clang-format on

}  // namespace detail

/** Increment the reference count of a `SHAMapItem`.
 *
 *  Called automatically by `boost::intrusive_ptr` when a new owning pointer
 *  is created. Guards against resurrection: if the count is already zero when
 *  the increment is attempted — indicating another thread concurrently released
 *  the last reference — `logicError` is called rather than silently
 *  resurrecting a dead object.
 *
 *  @param x The item whose reference count should be incremented.
 */
inline void
intrusive_ptr_add_ref(SHAMapItem const* x)
{
    if (x->refcount_++ == 0)
        logicError("SHAMapItem: the reference count is 0!");
}

/** Decrement the reference count of a `SHAMapItem`, destroying it when zero.
 *
 *  Called automatically by `boost::intrusive_ptr` when an owning pointer is
 *  destroyed or reset. On reaching zero, performs a two-phase teardown
 *  required by the struct-hack layout:
 *
 *  1. Explicitly calls `std::destroy_at` to run the `SHAMapItem` destructor
 *     (needed because `CountedObject`'s destructor decrements a global
 *     diagnostic counter and is not trivial). The `if constexpr` guard
 *     eliminates this call if the destructor chain ever becomes trivial.
 *  2. Returns the raw memory to `detail::gSlabber`. If the pointer was not
 *     slab-managed (allocated via the `new uint8_t[]` fallback), `deallocate`
 *     returns `false` and the memory is freed with `delete[]`.
 *
 *  @param x The item whose reference count should be decremented.
 */
inline void
intrusive_ptr_release(SHAMapItem const* x)
{
    if (--x->refcount_ == 0)
    {
        auto p = reinterpret_cast<std::uint8_t const*>(x);

        // The SHAMapItem destructor isn't trivial (because the destructor
        // for CountedObject isn't) so we can't avoid calling it here, but
        // plan for a future where we might not need to.
        if constexpr (!std::is_trivially_destructible_v<SHAMapItem>)
            std::destroy_at(x);

        // If the slabber doesn't claim this pointer, it was allocated
        // manually, so we free it manually.
        if (!detail::gSlabber.deallocate(const_cast<std::uint8_t*>(p)))
            delete[] p;
    }
}

/** Allocate and construct a new `SHAMapItem`.
 *
 *  The sole factory for `SHAMapItem` objects. Requests a slot from
 *  `detail::gSlabber` sized for `sizeof(SHAMapItem) + data.size()`, falling
 *  back to `new uint8_t[]` when no slab tier fits. The item is constructed
 *  in-place via placement new; the returned `intrusive_ptr` is initialised
 *  with `false` for the second argument to suppress the automatic reference
 *  increment — the constructor already sets `refcount_` to 1.
 *
 *  @param tag  The 256-bit key that identifies this item in the SHAMap trie.
 *  @param data The payload bytes to store; copied into the allocation.
 *  @return An owning `intrusive_ptr` to the newly created item with a
 *      reference count of 1.
 *  @note Asserts that `data.size() <= 16 MiB`.
 */
inline boost::intrusive_ptr<SHAMapItem>
makeShamapitem(uint256 const& tag, Slice data)
{
    XRPL_ASSERT(
        data.size() <= megabytes<std::size_t>(16), "xrpl::makeShamapitem : maximum input size");

    // NOLINTNEXTLINE(misc-const-correctness)
    std::uint8_t* raw = detail::gSlabber.allocate(data.size());

    // If we can't grab memory from the slab allocators, we fall back to
    // the standard library and try to grab a precisely-sized memory block:
    if (raw == nullptr)
        raw = new std::uint8_t[sizeof(SHAMapItem) + data.size()];

    // We do not increment the reference count here on purpose: the
    // constructor of SHAMapItem explicitly sets it to 1. We use the fact
    // that the refcount can never be zero before incrementing as an
    // invariant.
    return {new (raw) SHAMapItem{tag, data}, false};
}

static_assert(alignof(SHAMapItem) != 40);
static_assert(alignof(SHAMapItem) == 8 || alignof(SHAMapItem) == 4);

/** Produce a freshly allocated copy of an existing `SHAMapItem`.
 *
 *  Equivalent to `makeShamapitem(other.key(), other.slice())`. Provides the
 *  copy semantics that the class itself refuses to expose, yielding an
 *  independently owned item with the same key and payload.
 *
 *  @param other The item to copy.
 *  @return An owning `intrusive_ptr` to the newly allocated copy.
 */
inline boost::intrusive_ptr<SHAMapItem>
makeShamapitem(SHAMapItem const& other)
{
    return makeShamapitem(other.key(), other.slice());
}

}  // namespace xrpl
