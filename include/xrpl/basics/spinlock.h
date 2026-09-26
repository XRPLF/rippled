// Copyright (c) 2022, Nikolaos D. Bougalis <nikb@bougalis.net>

#pragma once

#include <xrpl/beast/utility/instrumentation.h>

#include <boost/predef/architecture.h>

#include <atomic>
#include <concepts>
#include <limits>
#include <type_traits>

#if BOOST_ARCH_X86
#include <immintrin.h>
#endif

namespace xrpl {

/** An unsigned integral type suitable for use as a spinlock.

    The type must be always lock-free when wrapped in std::atomic, so
    that lock operations cannot themselves take a (library-level) lock.
 */
template <typename T>
concept SpinlockValueType = std::is_unsigned_v<T> && std::atomic<T>::is_always_lock_free;

/** A spinlock value type that additionally supports the atomic bitwise
    operations required to pack multiple locks into a single integer.
 */
template <typename T>
concept PackedSpinlockValueType = SpinlockValueType<T> && requires(std::atomic<T>& a, T v) {
    { a.fetch_or(v) } -> std::same_as<T>;
    { a.fetch_and(v) } -> std::same_as<T>;
};

namespace detail {

/** Inform the processor that we are in a tight spin-wait loop.

    Spinlocks caught in tight loops can result in the processor's pipeline
    filling up with comparison operations, resulting in a misprediction at
    the time the lock is finally acquired, necessitating pipeline flushing
    which is ridiculously expensive and results in very high latency.

    This function instructs the processor to "pause" for some architecture
    specific amount of time, to prevent this.
 */
inline void
spinPause() noexcept
{
#if BOOST_ARCH_X86
    _mm_pause();
#elif BOOST_ARCH_ARM
    asm volatile("yield" ::: "memory");
#else
#error No implementation available for spinPause to use
#endif
}

}  // namespace detail

//------------------------------------------------------------------------------

/** Attempt to acquire a spinlock without blocking.

    @note This interface is primarily intended for one-shot attempts to
          acquire the lock. Avoid calling this function directly from a
          loop and use @ref spinLock instead.

    @tparam T An unsigned integral type.
    @param lock The atomic variable used as the lock.
    @return true if the lock was acquired, false if it was already held.
 */
template <SpinlockValueType T>
[[nodiscard]] bool
spinTryLock(std::atomic<T>& lock) noexcept
{
    // A compare-exchange is required here, not an unconditional exchange:
    // a failed attempt must not modify the lock word, in case the atomic
    // is shared with PackedSpinlock).
    T expected = 0;

    return lock.compare_exchange_strong(
        expected,
        std::numeric_limits<T>::max(),
        std::memory_order::acquire,
        std::memory_order::relaxed);
    ;
}

/** Acquire a spinlock, blocking until available.

    Uses a TTAS (test-and-test-and-set) pattern, so waiters share the cache
    line read-only, helping to avoid unnecessary coherency traffic.

    @tparam T An unsigned integral type.
    @param lock The atomic variable used as the lock.
 */
template <SpinlockValueType T>
void
spinLock(std::atomic<T>& lock) noexcept
{
    do
    {
        // Relaxed ordering is sufficient for the spin: this load is only
        // a filter. The acquire on the successful exchange in spinTryLock
        // is what synchronizes the critical section.
        while (lock.load(std::memory_order::relaxed) != 0)
            detail::spinPause();
    } while (!spinTryLock(lock));
}

/** Release a spinlock.

    @tparam T An unsigned integral type.
    @param lock The atomic variable used as the lock.
 */
template <SpinlockValueType T>
void
spinUnlock(std::atomic<T>& lock) noexcept
{
    lock.store(0, std::memory_order::release);
}

//------------------------------------------------------------------------------

/** A Lockable interface to a spinlock implemented on top of an atomic.

    @tparam T An unsigned integral type.

    @note Using `PackedSpinlock` and `Spinlock` against the same underlying
          atomic integer is possible but can result in `Spinlock` not being
          able to acquire the lock during periods of high contention due to
          the way the two locks operate: `Spinlock` spins and tries to grab
          all the bits at once, whereas any given `PackedSpinlock` instance
          only tries to grab one bit at a time. Caveat emptor.

    This class meets the requirements of Lockable:
        https://en.cppreference.com/w/cpp/named_req/Lockable
 */
template <SpinlockValueType T>
class Spinlock
{
    std::atomic<T>& lock_;

public:
    Spinlock(Spinlock const&) = delete;
    Spinlock&
    operator=(Spinlock const&) = delete;

    /** Construct a spinlock handle.

        @param lock The atomic integer to spin against.

        @note For performance reasons, you should strive to have `lock` be
              on a cacheline by itself.
     */
    explicit Spinlock(std::atomic<T>& lock) noexcept : lock_(lock)
    {
    }

    [[nodiscard]] bool
    try_lock() noexcept  // NOLINT(readability-identifier-naming)
    {
        return spinTryLock(lock_);
    }

    void
    lock() noexcept
    {
        spinLock(lock_);
    }

    void
    unlock() noexcept
    {
        spinUnlock(lock_);
    }
};

//------------------------------------------------------------------------------

/** A Lockable interface to a packed spinlock implemented on top of an atomic.

    Packed spinlocks offer tremendous space-efficient lock-sharding but
    they come at a cost.

    First, the implementation is necessarily low-level and uses advanced
    features like memory ordering and highly platform-specific tricks to
    maximize performance. This imposes a significant and ongoing cost to
    developers.

    Second, and perhaps most important, is that the packing of multiple
    locks into a single integer which, albeit space-efficient, also has
    performance implications stemming from data dependencies, increased
    cache-coherency traffic between processors and heavier loads on the
    processor's load/store units.

    To be sure, these locks can have advantages but they are definitely
    not general purpose locks and should not be thought of or used that
    way. The use cases for them are likely few and far between; without
    a compelling reason to use them, backed by profiling data, it might
    be best to use one of the standard locking primitives instead. Note
    that in most common platforms, `std::mutex` is so heavily optimized
    that it can, usually, outperform spinlocks.

    @tparam T An unsigned integral type (e.g. std::uint16_t)

    This class meets the requirements of Lockable:
        https://en.cppreference.com/w/cpp/named_req/Lockable
 */
template <PackedSpinlockValueType T>
class PackedSpinlock
{
    std::atomic<T>& bits_;
    T const mask_;

public:
    PackedSpinlock(PackedSpinlock const&) = delete;
    PackedSpinlock&
    operator=(PackedSpinlock const&) = delete;

    /** Construct a packed spinlock handle for a single bit.

        @param lock The atomic integer inside which the spinlock is packed.
        @param index The index of the spinlock this object acquires.

        @note For performance reasons, you should strive to have `lock` be
              on a cacheline by itself.
     */
    PackedSpinlock(std::atomic<T>& lock, int index) noexcept
        : bits_(lock), mask_([index]() {
            XRPL_ASSERT(
                index >= 0 && index < std::numeric_limits<T>::digits,
                "xrpl::PackedSpinlock::PackedSpinlock : valid index");
            return static_cast<T>(T{1} << index);
        }())
    {
    }

    [[nodiscard]] bool
    try_lock() noexcept  // NOLINT(readability-identifier-naming)
    {
        return (bits_.fetch_or(mask_, std::memory_order::acquire) & mask_) == 0;
    }

    void
    lock() noexcept
    {
        do
        {
            // The use of relaxed memory ordering here is intentional and
            // serves to help reduce cache coherency traffic during times
            // of contention by avoiding writes that are unlikely to grab
            // the requested lock.
            while ((bits_.load(std::memory_order::relaxed) & mask_) != 0)
                detail::spinPause();
        } while (!try_lock());
    }

    void
    unlock() noexcept
    {
        bits_.fetch_and(~mask_, std::memory_order::release);
    }
};

}  // namespace xrpl
