// Copyright (c) 2022, 2026, Nikolaos D. Bougalis <nikb@bougalis.net>

#pragma once

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/spinlock.h>

#include <boost/align.hpp>
#include <boost/core/type_name.hpp>
#include <boost/predef/os.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <concepts>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <utility>

#if BOOST_OS_LINUX
#include <sys/mman.h>
#endif

namespace xrpl::slab {

namespace detail {

/** Parking allows allocations and deallocations to complete without locking.

    Each slab carries a small array of "parking" spots that are accessed
    in a lock-free manner. Deallocation attempts will park an item in an
    available spot using CAS; allocation attempts will unpark an item by
    using an exchange. Both operations will fall back to the locked path
    if parking cannot service them.

    Parked items still count as outstanding because we cannot adjust the
    number of outstanding items without holding a lock. This is a detail
    that does not impact users of this allocator.

    Parking avoids more than the lock: pops from the freelist must first
    follow the link stored inside the item being popped, a load that can
    take a cache miss while the lock is held. Parked handoffs will never
    dereference the item; once parked, the memory is next touched by its
    new owner.

    When disabled, allocation and deallocation requests always go through
    the locked freelist path.
 */
inline constexpr bool allowParking = true;

/** The number of items in the parking.

    This value is here for documentation purposes and should not be
    adjusted; it has implications on the size and alignment of slab
    objects.
 */
inline constexpr std::size_t parkingCapacity = 4;

/** The minimum number of items per slab.

    It is hard to set a reasonable minimum value for an allocator
    that is intended as somewhat-general-purpose and which allows
    a wide range of sizes.

    When parking is enabled, a slab needs to have enough capacity
    to allocate at least parkingCapacity + 1 items.
 */
inline constexpr std::size_t minimumSlabItems = 4 * parkingCapacity;

/// Alignment for hugepage support.
inline constexpr std::size_t pageSize = megabytes<std::size_t>(2);

/** Allocate a new 2MB-aligned data buffer.

    @param size Size of the buffer to allocate in bytes.
    @return Pointer to allocated memory, or nullptr on failure.

    @note On Linux, advises the kernel to back the allocation with
          transparent huge pages if available.
 */
[[nodiscard, gnu::malloc]] inline std::uint8_t*
allocateBuffer(std::size_t size) noexcept
{
    auto ptr = static_cast<std::uint8_t*>(boost::alignment::aligned_alloc(pageSize, size));

#if BOOST_OS_LINUX
    if (ptr != nullptr) [[likely]]
        madvise(ptr, size, MADV_HUGEPAGE);
#endif

    return ptr;
}

/** Deallocate a buffer previously allocated by allocateBuffer.

    @param ptr Pointer to buffer, or nullptr (no-op).
 */
inline void
deallocateBuffer(std::uint8_t* ptr) noexcept
{
    boost::alignment::aligned_free(ptr);
}

//------------------------------------------------------------------------------

/** A node in the intrusive free list.

    Constructed in-place within free memory blocks. The next pointer
    is const because nodes are never modified after construction;
    they are simply constructed anew when reused.
 */
struct Item
{
    Item* const next;

    constexpr explicit Item(Item* n) noexcept : next(n)
    {
    }
};

// We need this to be trivially destructible so that we do not
// have to explicitly invoke the destructor when popping items
// from the free list or when deallocating the slab buffer.
static_assert(std::is_trivially_destructible_v<Item>);

/** A slab-sized buffer, not yet owned by any slab, arranged into a freelist.

    The buffer must be private to the constructing thread, because the
    freelist nodes are built with plain stores, and are only published
    to other threads when the buffer is grafted onto a slab.

 */
class BuiltBuffer
{
    std::uint8_t* base_ = nullptr;
    Item* head_ = nullptr;

public:
    BuiltBuffer(std::span<std::uint8_t> mem, std::size_t count) noexcept : base_(mem.data())
    {
        while (mem.size() >= count)
        {
            head_ = std::construct_at(reinterpret_cast<Item*>(mem.data()), head_);
            mem = mem.subspan(count);
        }
    }

    /** Transfer the freelist out; the buffer is then consumed. */
    [[nodiscard]] Item*
    take() noexcept
    {
        assert(head_ != nullptr);
        return std::exchange(head_, nullptr);
    }

    /** True once the freelist has been taken. */
    [[nodiscard]] bool
    consumed() const noexcept
    {
        return head_ == nullptr;
    }

    /** The base address associated with this buffer. */
    [[nodiscard]] std::uint8_t*
    base() const noexcept
    {
        return base_;
    }
};

/** A block of memory metadata for slab allocators.

    Each block manages a single slab buffer along with an associated free
    list. Blocks are then chained together to form a linked list of slabs
    per allocator.

    Blocks are sized and aligned to exactly one cache line: they contain
    heavily contended atomics and are handed out from a dense array (see
    SlabPool), so two blocks sharing a line would false-share, taxing
    every operation on one slab with coherence traffic caused by threads
    operating on its neighbor.

    @note SlabBlock instances are never destroyed or reclaimed once they
          are allocated to support lock-free iteration of lists of slabs
          without introducing ABA hazards. The data buffer which is held
          by an instance can be released independently.
 */
struct alignas(64) SlabBlock
{
    /** Head of the intrusive freelist of available items.

        This is only ever mutated while holding the lock; but what the
        atomic gets us is the ability to check whether the slab has an
        empty freelist without taking a lock. Stale reads are benign.
     */
    std::atomic<Item*> head = nullptr;

    /** The next slab in this allocator's list, or nullptr at the tail. */
    SlabBlock* const next = nullptr;

    /** The slab's data buffer, or nullptr if the slab has no buffer. */
    std::atomic<std::uint8_t*> data = nullptr;

    /** Spinlock guarding head, outstanding and cycles. */
    std::atomic<std::uint8_t> lock = 0;

    /** Number of buffer release cycles; retained for debugging. */
    std::uint16_t cycles = 0;

    /** Number of items from this slab's buffer not on the freelist. */
    std::uint32_t outstanding = 0;

    /** Recently freed items available for immediate, lock-free reuse. */
    std::array<std::atomic<std::uint8_t*>, parkingCapacity> parking{};

    /** Construct a block that is unlinked. */
    constexpr SlabBlock() noexcept = default;

    /** Construct a block with a link to the next block in the chain.

        @param n Pointer to the next block, or nullptr for the tail.
     */
    constexpr explicit SlabBlock(SlabBlock* n) noexcept : next(n)
    {
    }

    ~SlabBlock() = default;

    SlabBlock(SlabBlock const&) = delete;
    SlabBlock&
    operator=(SlabBlock const&) = delete;
    SlabBlock(SlabBlock&&) = delete;
    SlabBlock&
    operator=(SlabBlock&&) = delete;

    /** Determines whether this block owns the provided pointer. */
    [[nodiscard]] bool
    owns(std::uint8_t const* ptr, std::size_t size) const noexcept
    {
        assert(ptr != nullptr);

        auto const d = data.load(std::memory_order::acquire);

        if (d == nullptr)
            return false;

        auto const b = reinterpret_cast<std::uintptr_t>(d);
        auto const p = reinterpret_cast<std::uintptr_t>(ptr);

        return (p >= b) && (p < b + size);
    }

    /** Attempt to allocate from this block.

        @return Pointer to allocated memory, or nullptr if block is empty.
     */
    [[nodiscard]] std::uint8_t*
    tryAllocate() noexcept
    {
        // Quickly reject slabs that don't have a buffer without taking a
        // lock. The relaxed read may be stale, this is fine:
        //
        // - A stale null will only happen when we are racing a concurrent
        //   buffer assignment to this slab; the caller continues scanning
        //   other slabs and, if none can satisfy the request, the scan is
        //   re-run under the block lock, which closes the race.
        // - A stale non-null will fall through, executing the code which
        //   would have run if the check was not here in the first place.
        if (data.load(std::memory_order::relaxed) == nullptr)
            return nullptr;

        if constexpr (allowParking)
        {
            // Lookaside first: peek (read-only, so the line stays shared) at
            // the parked slots; only attempt an exchange, if we have a hit.
            for (auto& slot : parking)
            {
                if (slot.load(std::memory_order::relaxed) != nullptr)
                {
                    // We can race against, and lose to someone else; that is
                    // fine.
                    if (auto* p = slot.exchange(nullptr, std::memory_order::acquire))
                        return p;
                }
            }
        }

        Item* ret = nullptr;

        if (head.load(std::memory_order::relaxed) != nullptr)
        {
            spinLock(lock);

            ret = head.load(std::memory_order::relaxed);

            if (ret != nullptr)
            {
                head.store(ret->next, std::memory_order::relaxed);
                ++outstanding;
            }

            spinUnlock(lock);
        }

        return reinterpret_cast<std::uint8_t*>(ret);
    }

    /** Attempt to allocate from this block, while also offering it a buffer.

        Prefers existing capacity. If this slab has no buffer, it takes
        ownership of the buffer we provided and satisfies the request.

        @param bb A built buffer.

        @return Pointer to allocated memory, or nullptr.
     */
    [[nodiscard]] std::uint8_t*
    tryAllocate(BuiltBuffer& bb) noexcept
    {
        if (auto ret = tryAllocate())
            return ret;

        if (std::uint8_t* expected = nullptr; !data.compare_exchange_strong(
                expected, bb.base(), std::memory_order::release, std::memory_order::relaxed))
            return nullptr;

        // Pops an item from the freelist and accounts for the item.
        auto const pop = [this]() noexcept {
            auto* item = head.load(std::memory_order::relaxed);
            assert(item != nullptr);

            head.store(item->next, std::memory_order::relaxed);
            ++outstanding;

            return reinterpret_cast<std::uint8_t*>(item);
        };

        spinLock(lock);

        assert(head.load(std::memory_order::relaxed) == nullptr && outstanding == 0);

        // Take ownership of this buffer
        head.store(bb.take(), std::memory_order::relaxed);

        if constexpr (allowParking)
        {
            // Pre-populate the parking: with "parking full" as the slab's
            // resting state, the final free finds no empty slot and takes
            // the lock, which will also release the parked slots.
            for (auto& slot : parking)
                slot.store(pop(), std::memory_order::release);
        }

        auto ret = pop();

        spinUnlock(lock);

        return ret;
    }

    /** Attempt to return a pointer to this block.

        @param ptr Pointer to memory block.
        @param slabSize Size of the slab's data buffer.
        @param releaseBuffer Callback invoked with a pointer to the block's
                             buffer so that it can be released.
        @return true if ptr belonged to this block and was freed,
                false otherwise.
     */
    template <typename ReleaseFunc>
        requires std::is_nothrow_invocable_v<ReleaseFunc&, std::uint8_t*>
    [[nodiscard]] bool
    tryDeallocate(std::uint8_t* ptr, std::size_t slabSize, ReleaseFunc&& releaseBuffer) noexcept
    {
        if (!owns(ptr, slabSize))
            return false;

        if constexpr (allowParking)
        {
            // Try to see if we can park this item without grabbing the
            // lock. If we succeed, a future allocation will be able to
            // satisfy an allocation request without grabbing a lock.
            for (auto& slot : parking)
            {
                if (auto x = slot.load(std::memory_order::relaxed); x == nullptr)
                {
                    if (slot.compare_exchange_strong(
                            x, ptr, std::memory_order::release, std::memory_order::relaxed))
                        return true;
                }
            }
        }

        std::uint8_t* buf = nullptr;

        spinLock(lock);

        assert(ptr != nullptr);
        assert(outstanding > 0);

        // Pushes an item onto the freelist. Must hold lock to call.
        auto const push = [this](std::uint8_t* p) noexcept {
            head.store(
                std::construct_at(
                    reinterpret_cast<Item*>(p), head.load(std::memory_order::relaxed)),
                std::memory_order::relaxed);
        };

        if constexpr (allowParking)
        {
            // Sweep the parked slots only when this could result in the slab
            // having no outstanding allocations: that is, when the number of
            // outstanding items is equal to the number of parking spots plus
            // one (for the item we are freeing) and the slab isn't the first
            // block.
            if (outstanding == parking.size() + 1 && next != nullptr)
            {
                // Because the parked slots are handled lock-free, we must be
                // extra vigilant, since someone could have allocated an item
                // from there.
                for (auto& slot : parking)
                {
                    if (auto park = slot.exchange(nullptr, std::memory_order::acquire))
                    {
                        push(park);
                        --outstanding;
                    }
                }
            }
        }

        push(ptr);

        // If this block became empty, and it is not the first block, we
        // can release its buffer. The first block (which is at the tail
        // of the list with next == nullptr) is exempt from this policy
        // so that we can always have one block ready.
        if (--outstanding == 0 && next != nullptr)
        {
            head.store(nullptr, std::memory_order::relaxed);
            buf = data.exchange(buf, std::memory_order::relaxed);
            cycles += (cycles < std::numeric_limits<std::uint16_t>::max());
        }

        spinUnlock(lock);

        if (buf != nullptr)
            releaseBuffer(buf);

        return true;
    }
};

static_assert(sizeof(SlabBlock) == 64 && alignof(SlabBlock) == 64);

//------------------------------------------------------------------------------

/** Global pool of SlabBlock objects shared by all slab allocators.

    Provides a fixed-size array of pre-allocated slabs with fallback
    to heap allocation if exhausted.
 */
class SlabPool
{
    std::array<SlabBlock, 32768> blocks_{};
    std::atomic<std::size_t> count_{0};

public:
    constexpr SlabPool() = default;

    SlabPool(SlabPool const&) = delete;
    SlabPool&
    operator=(SlabPool const&) = delete;
    SlabPool(SlabPool&&) = delete;
    SlabPool&
    operator=(SlabPool&&) = delete;

    /** Acquire a slab from the pool.

        @param next Pointer to link as the slab's next pointer.
        @return Pointer to acquired block, or nullptr if allocation failed.
     */
    [[nodiscard]] SlabBlock*
    acquire(SlabBlock* next) noexcept
    {
        auto idx = count_.load(std::memory_order::relaxed);

        while (idx < blocks_.size())
        {
            if (count_.compare_exchange_weak(idx, idx + 1, std::memory_order::relaxed))
            {
                return std::construct_at(&blocks_[idx], next);
            }
        }

        // Pool exhausted (unlikely!) so fall back to heap
        return new (std::nothrow) SlabBlock(next);
    }
};

/// Single global block pool for all slab allocators.
inline constinit SlabPool gSlabPool;

}  // namespace detail
//------------------------------------------------------------------------------

/** A slab allocator for fixed-size memory blocks.

    Allocates memory in large slabs (multiples of 2MB) and sub-allocates
    fixed-size blocks from them. Provides fast allocation with minimal
    fragmentation for objects of uniform size.

    @tparam Type The type used to determine minimum block size and alignment.
    @tparam Align Alignment for allocated blocks (must be >= alignof(Type)).
 */
template <typename Type, std::size_t Align = alignof(Type)>
    requires(
        std::has_single_bit(Align) && sizeof(Type) >= sizeof(detail::Item) &&
        Align >= std::max(alignof(detail::Item), alignof(Type)))
class alignas(64) SizedAllocator
{
    /// Linked list of slabs for this allocator.
    std::atomic<detail::SlabBlock*> slabs_{nullptr};

    /** A spare slab-sized buffer retained for reuse, or nullptr.

        When a slab drains completely, its buffer is released; when demand
        returns, a slab needs a buffer again. If we are operating near the
        boundary of a slab, we may end having to release and reacquire the
        buffer repeatedly. By caching the released buffer, we can reuse it
        directly for the expansion, avoiding that overhead.
     */
    std::atomic<std::uint8_t*> cachedBuffer_{nullptr};

    /** The slab that we used most recently allocated memory from.

        This slab exists in the list of slabs_, but may be deep enough
        that it requires a lot of work to get to. By caching a pointer
        to it, we can check it preferentially, which helps keep memory
        hot.
     */
    std::atomic<detail::SlabBlock*> cachedSlab_{nullptr};

    /** Item size (sizeof(Type) + extra, rounded up for alignment). */
    std::size_t const itemSize_;

    /** Size of each slab's data buffer (multiple of 2 MB). */
    std::size_t const slabSize_;

    /** Spinlock to serialize the slow allocation path. */
    std::atomic<std::uint8_t> blockLock_{0};

    /** Retain a slab-sized buffer for later reuse, or release it.

        The allocator keeps at most one spare buffer, so that any
        workloads oscillating around a slab boundary, do not have
        to round-trip through the operating system.

        @param buf A buffer of exactly slabSize_ bytes that was
                   obtained from allocateBuffer, or nullptr (no-op).
     */
    void
    discard(std::uint8_t* buf) noexcept
    {
        if (buf == nullptr)
            return;

        if (std::uint8_t* expected = nullptr;
            !cachedBuffer_.compare_exchange_strong(expected, buf, std::memory_order::release))
            detail::deallocateBuffer(buf);
    }

    /** Allocate a memory block from the existing slabs.

        Probes each slab in the list, except the one passed in, which the
        caller already tried. If a slab successfully services the request,
        we store it into cachedSlab_, to preferentially use it.

        This does not acquire the blockLock_, but can take the individual
        slab locks during traversal.

        @param hint The slab the caller already tried to allocate memory
                    from, if any. We skip it if we come across it.

        @return Pointer to allocated memory, or nullptr if no existing
                slab can satisfy the request.
     */
    [[nodiscard]] std::uint8_t*
    fastAllocate(detail::SlabBlock const* hint) noexcept
    {
        auto slab = slabs_.load(std::memory_order::acquire);

        while (slab != nullptr)
        {
            if (slab != hint)
            {
                if (auto ret = slab->tryAllocate())
                {
                    cachedSlab_.store(slab, std::memory_order::release);
                    return ret;
                }
            }

            slab = slab->next;
        }

        return nullptr;
    }

    /** Allocate a memory block, potentially adding new capacity.

        Must be called with blockLock_ held.

        @param bb A built buffer, private to the calling thread. The
                  buffer may or may not be consumed.

        @return Pointer to allocated memory, or nullptr on failure.
     */
    [[nodiscard]] std::uint8_t*
    slowAllocate(detail::BuiltBuffer& bb) noexcept
    {
        // Try to allocate again; if a slab is empty, offer it the newly
        // allocated memory.
        auto slab = slabs_.load(std::memory_order::acquire);

        while (slab != nullptr)
        {
            if (auto ret = slab->tryAllocate(bb))
            {
                cachedSlab_.store(slab, std::memory_order::release);
                return ret;
            }

            slab = slab->next;
        }

        // No existing slab could take the buffer; get a new slab from
        // the global pool.
        slab = detail::gSlabPool.acquire(slabs_.load(std::memory_order::relaxed));

        if (slab == nullptr) [[unlikely]]
            return nullptr;

        // This is a fresh slab, so it must accept the buffer we provided
        // it with and return a valid pointer.
        auto ret = slab->tryAllocate(bb);
        assert(ret != nullptr && slab->owns(ret, slabSize_));

        cachedSlab_.store(slab, std::memory_order::release);
        slabs_.store(slab, std::memory_order::release);

        return ret;
    }

public:
    /** Construct a slab allocator.

        @param extra Extra bytes per item beyond sizeof(Type).
        @param minItems Minimum number of items per slab (rounded up to 2MB).
     */
    constexpr explicit SizedAllocator(std::size_t extra, std::size_t minItems) noexcept
        : itemSize_(boost::alignment::align_up(sizeof(Type) + extra, Align))
        , slabSize_(
              boost::alignment::align_up(
                  itemSize_ * std::max(minItems, detail::minimumSlabItems),
                  detail::pageSize))
    {
    }

    ~SizedAllocator() noexcept
    {
        // We can free the cached buffer, if one is available. As for the data
        // buffers, they are intentionally not released: C++ destruction order
        // does not allow us to assume that all items that were allocated from
        // this allocator have been freed by the time this destructor runs.
        detail::deallocateBuffer(cachedBuffer_.exchange(nullptr));
    }

    SizedAllocator(SizedAllocator const&) = delete;
    SizedAllocator&
    operator=(SizedAllocator const&) = delete;
    SizedAllocator(SizedAllocator&&) = delete;
    SizedAllocator&
    operator=(SizedAllocator&&) = delete;

    /** Returns the size of memory blocks returned by this allocator.

        @return Size of each allocated block in bytes.
     */
    [[nodiscard]] constexpr std::size_t
    size() const noexcept
    {
        return itemSize_;
    }

    /** Allocate a memory block.

        @return Pointer to allocated memory, or nullptr on failure.

        @note The gnu::malloc attribute is an optimization hint that can
              be leveraged by GCC and Clang.
     */
    [[nodiscard, gnu::malloc]] void*
    allocate() noexcept
    {
        // Check if the cached slab has memory available:
        auto const hint = cachedSlab_.load(std::memory_order::acquire);

        if (hint != nullptr)
        {
            if (auto ret = hint->tryAllocate())
                return ret;
        }

        // Fast path: try existing slabs
        if (auto ret = fastAllocate(hint))
            return ret;

        // Prepare capacity before taking the lock: obtain and build a
        // buffer with no locks held.
        auto buf = cachedBuffer_.exchange(nullptr, std::memory_order::acquire);

        if (buf == nullptr)
        {
            buf = detail::allocateBuffer(slabSize_);

            if (buf == nullptr) [[unlikely]]
                return nullptr;
        }

        detail::BuiltBuffer bb(std::span{buf, slabSize_}, itemSize_);

        spinLock(blockLock_);
        auto ret = slowAllocate(bb);
        spinUnlock(blockLock_);

        // It is possible that the slowAllocate used existing memory, which
        // became available after the fastAllocate above. If so, the buffer
        // we allocated can be discarded.
        if (!bb.consumed())
            discard(bb.base());

        return ret;
    }

    /** Return a memory block to the allocator.

        @param ptr Pointer to memory block.
        @return true if the block belonged to this allocator and was freed.
     */
    [[nodiscard]] bool
    deallocate(void* ptr) noexcept
    {
        assert(ptr != nullptr);

        auto p = static_cast<std::uint8_t*>(ptr);

        auto const release = [this](std::uint8_t* buf) noexcept { discard(buf); };

        auto const hint = cachedSlab_.load(std::memory_order::acquire);

        if (hint != nullptr)
        {
            if (hint->tryDeallocate(p, slabSize_, release))
                return true;
        }

        auto slab = slabs_.load(std::memory_order::acquire);

        while (slab != nullptr)
        {
            if (slab != hint && slab->tryDeallocate(p, slabSize_, release))
                return true;

            slab = slab->next;
        }

        return false;
    }

    [[nodiscard]] bool
    deallocate(void const* ptr) noexcept
    {
        return deallocate(const_cast<void*>(ptr));
    }
};

//------------------------------------------------------------------------------
/** Requirements for a fallback policy used by AlignedAllocator.

    A fallback handles requests the slab bins cannot satisfy. Policies
    must be constructible in a constant expression (the allocator is
    typically a constinit global) and all operations must be noexcept.
 */
template <typename T>
concept FallbackAllocator = std::is_nothrow_default_constructible_v<T> &&
    requires(T f, void* p, std::size_t align, std::size_t size) {
        { f.allocate(align, size) } noexcept -> std::same_as<void*>;
        { f.deallocate(p) } noexcept -> std::same_as<bool>;
    };

/** Fallback policy that routes to the aligned heap.

    Requests the slab bins cannot satisfy are served by the general
    purpose allocator; deallocation is total.
 */
struct HeapFallback
{
    /** Allocate size bytes aligned to align.

        @return Pointer to allocated memory, or nullptr on failure.
     */
    [[nodiscard, gnu::malloc]] void*
    allocate(std::size_t align, std::size_t size) noexcept
    {
        assert(size != 0);
        assert(std::has_single_bit(align));

        return boost::alignment::aligned_alloc(align, size);
    }

    /** Release memory previously obtained from allocate.

        @return true, unconditionally: this policy handles every pointer
                given to it.
     */
    bool
    deallocate(void* ptr) noexcept
    {
        boost::alignment::aligned_free(ptr);
        return true;
    }
};

/** Fallback policy that refuses requests.

    Allocation beyond the slab bins fails with nullptr; deallocation of
    memory the bins do not own is reported back to the caller, which is
    responsible for releasing it through whatever mechanism produced it.
 */
struct NoFallback
{
    [[nodiscard]] void*
    allocate(std::size_t, std::size_t) noexcept
    {
        return nullptr;
    }

    [[nodiscard]] bool
    deallocate(void*) noexcept
    {
        return false;
    }
};

static_assert(FallbackAllocator<HeapFallback>);
static_assert(FallbackAllocator<NoFallback>);

//------------------------------------------------------------------------------

/** Configuration for a single slab allocator.

    @tparam MinItems Minimum number of items per slab (must be > 0).
    @tparam Extra Extra bytes per item beyond sizeof(Type).
 */
template <std::size_t MinItems, std::size_t Extra = 0>
    requires(MinItems > 0)
struct Config
{
    static constexpr std::size_t minItems = MinItems;
    static constexpr std::size_t extra = Extra;
};

/** Concept for valid slab configuration types. */
template <typename T>
concept SlabConfig = requires {
    { T::minItems } -> std::convertible_to<std::size_t>;
    { T::extra } -> std::convertible_to<std::size_t>;
    requires T::minItems > 0;
};

/** Validate slab configurations at compile time.

    Checks that configurations produce strictly increasing sizes after
    alignment. This catches both unsorted configs and configs that
    collapse to the same size after alignment.

    @tparam Type The type used to determine base size.
    @tparam Align Alignment for allocated blocks.
    @tparam Configs Configuration types to validate.
    @return true if configurations are valid, false otherwise.
 */
template <typename Type, std::size_t Align, SlabConfig... Configs>
consteval bool
validateSlabConfig()
{
    constexpr auto alignUp = [](std::size_t n, std::size_t a) { return (n + a - 1) & ~(a - 1); };

    constexpr std::array<std::size_t, sizeof...(Configs)> sizes{
        alignUp(sizeof(Type) + Configs::extra, Align)...};

    for (std::size_t i = 1; i < sizes.size(); ++i)
    {
        if (sizes[i - 1] >= sizes[i])
            return false;
    }

    return true;
}

//------------------------------------------------------------------------------

/** A collection of slab allocators for different sizes.

    Manages multiple SizedAllocator instances, each configured for a
    different block size. Allocations are routed to the smallest allocator
    that can satisfy the request.

    @tparam Type The type used to determine minimum block size and alignment.
    @tparam Align Alignment for allocated blocks (must be >= alignof(Type)).
    @tparam Fallback Type satisfying FallbackAllocator to handle allocations
                     that cannot be satisfied by any of the size classes.
    @tparam Configs Configuration types specifying each size class.
 */
template <
    typename Type,
    std::size_t Align,
    FallbackAllocator Fallback = HeapFallback,
    SlabConfig... Configs>
    requires(
        sizeof(Type) >= sizeof(void*) && Align >= alignof(Type) && std::has_single_bit(Align) &&
        sizeof...(Configs) > 0 && validateSlabConfig<Type, Align, Configs...>())
class AlignedAllocator
{
    std::array<SizedAllocator<Type, Align>, sizeof...(Configs)> allocators_;
    std::size_t const exmax_;
    Fallback fallback_;

public:
    /** Construct an allocator set.

        @param fallback Policy handling requests the bins cannot satisfy.
     */
    constexpr AlignedAllocator(Fallback fallback = {}) noexcept
        : allocators_{SizedAllocator<Type, Align>(Configs::extra, Configs::minItems)...}
        , exmax_(allocators_.back().size() - sizeof(Type))
        , fallback_(std::move(fallback))
    {
    }

    ~AlignedAllocator() = default;

    AlignedAllocator(AlignedAllocator const&) = delete;
    AlignedAllocator&
    operator=(AlignedAllocator const&) = delete;
    AlignedAllocator(AlignedAllocator&&) = delete;
    AlignedAllocator&
    operator=(AlignedAllocator&&) = delete;

    /** Allocate memory for an object with extra bytes.

        @param extra Extra bytes needed beyond sizeof(Type).
        @return Pointer to memory, or nullptr if no suitable allocator
                or allocation failed.

        @note The gnu::malloc attribute is an optimization hint that can
              be leveraged by GCC and Clang.
     */
    [[nodiscard, gnu::malloc]] void*
    allocate(std::size_t extra = 0) noexcept
    {
        if (extra <= exmax_) [[likely]]
        {
            auto const size = sizeof(Type) + extra;

            for (auto& a : allocators_)
            {
                if (a.size() >= size)
                {
                    if (auto p = a.allocate())
                        return p;

                    break;
                }
            }
        }

        return fallback_.allocate(Align, sizeof(Type) + extra);
    }

    /** Return memory to the allocator set.

        @param ptr Pointer to memory block.
        @param extra The number of extra bytes requested via allocate.
        @return true if the memory has been freed.

        @note When the sized overloads are used, extra must be precisely
              equal to the value passed to allocate; supplying the wrong
              size can cause the function to incorrectly decide that the
              memory did not belong to this set, which, depending on the
              fallback configuration and/or the caller's behavior, might
              result in memory corruption or worse.
     */
    /** @{ */
    [[nodiscard]] bool
    deallocate(void* ptr) noexcept
    {
        assert(ptr != nullptr);

        for (auto& a : allocators_)
        {
            if (a.deallocate(ptr))
                return true;
        }

        return fallback_.deallocate(ptr);
    }

    [[nodiscard]] bool
    deallocate(void const* ptr) noexcept
    {
        return deallocate(const_cast<void*>(ptr));
    }

    [[nodiscard]] bool
    deallocate(void* ptr, std::size_t extra) noexcept
    {
        if (extra <= exmax_) [[likely]]
        {
            auto const size = sizeof(Type) + extra;

            for (auto& a : allocators_)
            {
                // The first bucket large enough to have allocated this
                // must be the owner. If it is not, we did not allocate
                // this pointer.
                if (a.size() >= size)
                {
                    if (a.deallocate(ptr))
                        return true;

                    break;
                }
            }
        }

        return fallback_.deallocate(ptr);
    }

    [[nodiscard]] bool
    deallocate(void const* ptr, std::size_t extra) noexcept
    {
        return deallocate(const_cast<void*>(ptr), extra);
    }
    /** @} */
};

/** Alias for AlignedAllocator with default alignment. */
template <typename Type, FallbackAllocator Fallback = HeapFallback, SlabConfig... Configs>
using Allocator = AlignedAllocator<Type, alignof(Type), Fallback, Configs...>;

}  // namespace xrpl::slab
