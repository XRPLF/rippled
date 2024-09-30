/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2022, Nikolaos D. Bougalis <nikb@bougalis.net>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#ifndef RIPPLE_BASICS_SPINLOCK_H_INCLUDED
#define RIPPLE_BASICS_SPINLOCK_H_INCLUDED

#include <atomic>
#include <cassert>
#include <limits>
#include <type_traits>

#ifndef __aarch64__
#include <immintrin.h>
#endif

namespace ripple {

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
spin_pause() noexcept
{
#ifdef __aarch64__
    asm volatile("yield");
#else
    _mm_pause();
#endif
}

}  // namespace detail

/** @{ */
/** Classes to handle arrays of spinlocks packed into a single atomic integer:

    Packed spinlocks allow for tremendously space-efficient lock-sharding
    but they come at a cost.

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
 */

/** A class that grabs a single packed spinlock from an atomic integer.

    This class meets the requirements of Lockable:
        https://en.cppreference.com/w/cpp/named_req/Lockable
 */
template <class T>
class packed_spinlock
{
    // clang-format off
    static_assert(std::is_unsigned_v<T>);
    static_assert(std::atomic<T>::is_always_lock_free);
    static_assert(
        std::is_same_v<decltype(std::declval<std::atomic<T>&>().fetch_or(0)), T> &&
        std::is_same_v<decltype(std::declval<std::atomic<T>&>().fetch_and(0)), T>,
        "std::atomic<T>::fetch_and(T) and std::atomic<T>::fetch_and(T) are required by packed_spinlock");
    // clang-format on

private:
    std::atomic<T>& bits_;
    T const mask_;

public:
    packed_spinlock(packed_spinlock const&) = delete;
    packed_spinlock&
    operator=(packed_spinlock const&) = delete;

    /** A single spinlock packed inside the specified atomic

        @param lock The atomic integer inside which the spinlock is packed.
        @param index The index of the spinlock this object acquires.

        @note For performance reasons, you should strive to have `lock` be
              on a cacheline by itself.
     */
    packed_spinlock(std::atomic<T>& lock, int index)
        : bits_(lock), mask_(static_cast<T>(1) << index)
    {
        assert(index >= 0 && (mask_ != 0));
    }

    [[nodiscard]] bool
    try_lock()
    {
        return (bits_.fetch_or(mask_, std::memory_order_acquire) & mask_) == 0;
    }

    void
    lock()
    {
        while (!try_lock())
        {
            // The use of relaxed memory ordering here is intentional and
            // serves to help reduce cache coherency traffic during times
            // of contention by avoiding writes that would definitely not
            // result in the lock being acquired.
            while ((bits_.load(std::memory_order_relaxed) & mask_) != 0)
                detail::spin_pause();
        }
    }

    void
    unlock()
    {
        bits_.fetch_and(~mask_, std::memory_order_release);
    }
};

/** A spinlock implemented on top of an atomic integer.

    @note Using `packed_spinlock` and `spinlock` against the same underlying
          atomic integer can result in `spinlock` not being able to actually
          acquire the lock during periods of high contention, because of how
          the two locks operate: `spinlock` will spin trying to grab all the
          bits at once, whereas any given `packed_spinlock` will only try to
          grab one bit at a time. Caveat emptor.

    This class meets the requirements of Lockable:
        https://en.cppreference.com/w/cpp/named_req/Lockable
 */
template <class T>
class spinlock
{
    static_assert(std::is_unsigned_v<T>);
    static_assert(std::atomic<T>::is_always_lock_free);

private:
    std::atomic<T>& lock_;

public:
    spinlock(spinlock const&) = delete;
    spinlock&
    operator=(spinlock const&) = delete;

    /** Grabs the

        @param lock The atomic integer to spin against.

        @note For performance reasons, you should strive to have `lock` be
              on a cacheline by itself.
     */
    spinlock(std::atomic<T>& lock) : lock_(lock)
    {
    }

    [[nodiscard]] bool
    try_lock()
    {
        T expected = 0;

        return lock_.compare_exchange_weak(
            expected,
            std::numeric_limits<T>::max(),
            std::memory_order_acquire,
            std::memory_order_relaxed);
    }

    void
    lock()
    {
        while (!try_lock())
        {
            // The use of relaxed memory ordering here is intentional and
            // serves to help reduce cache coherency traffic during times
            // of contention by avoiding writes that would definitely not
            // result in the lock being acquired.
            while (lock_.load(std::memory_order_relaxed) != 0)
                detail::spin_pause();
        }
    }

    void
    unlock()
    {
        lock_.store(0, std::memory_order_release);
    }
};
/** @} */

}  // namespace ripple

#endif
