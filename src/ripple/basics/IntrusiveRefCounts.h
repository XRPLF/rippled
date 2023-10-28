//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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
//==============================================================================

#ifndef RIPPLE_BASICS_INTRUSIVEREFCOUNTS_H_INCLUDED
#define RIPPLE_BASICS_INTRUSIVEREFCOUNTS_H_INCLUDED

#include <atomic>
#include <cassert>
#include <cstdint>

namespace ripple {

enum class ReleaseRefAction { noop, partialDestroy, destroy };

// This can be embedded in classes to implement the two atomic counts
struct IntrusiveRefCounts
{
    virtual ~IntrusiveRefCounts() noexcept;

    // This must be `noexcept` or the make_SharedIntrusive function could leak
    // memory.
    void
    addStrongRef() const noexcept;

    void
    addWeakRef() const noexcept;

    ReleaseRefAction
    releaseStrongRef() const;

    ReleaseRefAction
    releaseWeakRef() const;

    // Returns true is able to checkout a strong ref. False otherwise
    bool
    checkoutStrongRefFromWeak() const noexcept;

    bool
    expired() const noexcept;

    std::size_t
    use_count() const noexcept;

    // This function MUST be called after a partial destructor finishes running.
    // Calling this function may cause other threads to delete the object
    // pointed to by `o`, so `o` should never be used after calling this
    // function. The parameter will be set to a `nullptr` after calling this
    // function to emphasize that it should not be used.
    // Note: This is intentionally NOT called at the end of `partialDestructor`.
    // The reason for this is if new classes are written to support this smart
    // pointer class, they need to write their own `partialDestructor` function
    // and ensure `partialDestructorFinished` is called at the end. Putting this
    // call inside the smart pointer class itself is expected to be less error
    // prone.
    // Note: The "two-star" programming is intentional. It emphasizes that `o`
    // may be deleted and the unergonomic API is meant to signal the special
    // nature of this function call to callers.
    // Note: This is a template to support incompletely defined classes.
    template <class T>
    friend void
    partialDestructorFinished(T** o);

private:
    // TODO: Check what expected counts are. Probably more bits for strong

    /** `refCounts` consists of four fields that are treated atomically:

         1. Strong count. This is a count of the number of shared pointers that
         hold a reference to this object. When the strong counts goes to zero,
         if the weak count is zero, the destructor is run. If the weak count is
         non-zero when the strong count goes to zero then the partialDestructor
         is run.

         2. Weak count. This is a count of the number of weak pointer that hold
         a reference to this object. When the weak count goes to zero and the
         strong count is also zero, then the destructor is run.

         3. Partial destroy started bit. This bit is set if the
         `partialDestructor` function has been started (or is about to be
         started). This is used to prevent the destructor from running
         concurrently with the partial destructor. This can easily happen when
         the last strong pointer release its reference in one thread and starts
         the partialDestructor, while in another thread the last weak pointer
         goes out of scope and starts the destructor while the partialDestructor
         is still running. Both a start and finished bit is needed to handle a
         corner-case where the last strong pointer goes out of scope, then then
         last `weakPointer` goes out of scope, but this happens before the
         `partialDestructor` bit is set. It would be possible to use a single
         bit if it could also be set atomically when the strong count goes to
         zero and the weak count is non-zero, but that would add complexity (and
         likely slow down common cases as well).

         4. Partial destroy finished bit. This bit is set when the
         `partialDestructor` has finished running. See (3) above for more
         information.

         */
    mutable std::atomic<std::uint32_t> refCounts{0};

    /**  Amount to change the strong count when adding or releasing a reference

         Note: The strong count is stored in the low 16 bits of refCounts
      */
    static constexpr std::uint32_t strongDelta = 1;

    /**  Amount to change the strong count when adding or releasing a reference

         Note: The strong count is stored in the high 14 bits of refCounts
      */
    static constexpr std::uint32_t weakDelta = (1 << 16);

    /**  Flag that is set when the partialDestroy function has started running
         (or is about to start running).

         See description of the `refCounts` field for a fuller description of
         this field.
      */
    static constexpr std::uint32_t partialDestroyStartedMask = (1 << 31);

    /**  Flag that is set when the partialDestroy function has finished running

         See description of the `refCounts` field for a fuller description of
         this field.
      */
    static constexpr std::uint32_t partialDestroyFinishedMask = (1 << 30);

    /** Mask that will zero out all the `count` bits and leave the tag bits
        unchanged.
      */
    static constexpr std::uint32_t tagMask =
        partialDestroyStartedMask | partialDestroyFinishedMask;

    /** Mask that will zero out the `tag` bits and leave the count bits
        unchanged.
      */
    static constexpr std::uint32_t valueMask = ~tagMask;

    /** Mask that will zero out everything except the strong count.
     */
    static constexpr std::uint32_t strongMask = 0xffff & valueMask;

    /** Mask that will zero out everything except the weak count.
     */
    static constexpr std::uint32_t weakMask = 0xffff0000 & valueMask;

    /** Unpack the count and tag fields from the packed atomic integer form. */
    struct RefCountPair
    {
        std::uint32_t strong;
        std::uint32_t weak;
        /**  The `partialDestroyStartedBit` is set to on when the partial
             destroy function is started. It is not a boolean; it is a uint32
             with all bits zero with the possible exception of the
             `partialDestroyStartedMask` bit. This is done so it can be directly
             masked into the `combinedValue`.
         */
        std::uint32_t partialDestroyStartedBit{0};
        /**  The `partialDestroyFinishedBit` is set to on when the partial
             destroy function has finished.
         */
        std::uint32_t partialDestroyFinishedBit{0};
        RefCountPair(std::uint32_t v) noexcept;
        RefCountPair(std::uint32_t s, std::uint32_t w) noexcept;

        /** Convert back to the packed integer form. */
        std::uint32_t
        combinedValue() const noexcept;

        static constexpr std::uint32_t maxStrongValue = (1 << 16) - 1;
        static constexpr std::uint32_t maxWeakValue = (1 << 14) - 1;
        /**  Put an extra margin to detect when running up against limits */
        static constexpr std::uint32_t checkStrongMaxValue =
            maxStrongValue - 32;
        static constexpr std::uint32_t checkWeakMaxValue = maxWeakValue - 32;
    };
};

inline void
IntrusiveRefCounts::addStrongRef() const noexcept
{
    refCounts.fetch_add(strongDelta, std::memory_order_acq_rel);
}

inline void
IntrusiveRefCounts::addWeakRef() const noexcept
{
    refCounts.fetch_add(weakDelta, std::memory_order_acq_rel);
}

inline ReleaseRefAction
IntrusiveRefCounts::releaseStrongRef() const
{
    RefCountPair prevVal =
        refCounts.fetch_sub(strongDelta, std::memory_order_acq_rel);

    if (prevVal.strong == 1)
    {
        if (prevVal.weak == 0)
        {
            // Can't be in partial destroy because only decrementing the strong
            // count to zero can start a partial destroy, and that can't happen
            // twice.
            assert(!prevVal.partialDestroyStartedBit);
            return ReleaseRefAction::destroy;
        }
        else
        {
            auto p = refCounts.fetch_or(partialDestroyStartedMask);
            (void)p;
            assert(!(p & partialDestroyStartedMask));
            return ReleaseRefAction::partialDestroy;
        }
    }
    return ReleaseRefAction::noop;
}

inline ReleaseRefAction
IntrusiveRefCounts::releaseWeakRef() const
{
    auto prevIntVal = refCounts.fetch_sub(weakDelta, std::memory_order_acq_rel);
    RefCountPair prev = prevIntVal;
    if (prev.weak == 1 && prev.strong == 0)
    {
        if (!prev.partialDestroyStartedBit)
        {
            // Very unlikely to hit this case
            refCounts.wait(prevIntVal, std::memory_order_acq_rel);
            prevIntVal = refCounts.load(std::memory_order_acquire);
            prev = RefCountPair{prevIntVal};
        }
        if (!prev.partialDestroyFinishedBit)
        {
            // partial destroy MUST finish before running a full destroy (when
            // using weak pointers)
            refCounts.wait(prevIntVal, std::memory_order_acq_rel);
        }
        return ReleaseRefAction::destroy;
    }
    return ReleaseRefAction::noop;
}

inline bool
IntrusiveRefCounts::checkoutStrongRefFromWeak() const noexcept
{
    auto curValue = RefCountPair{1, 1}.combinedValue();
    auto desiredValue = RefCountPair{2, 1}.combinedValue();

    // TODO double check memory orders
    while (!refCounts.compare_exchange_weak(
        curValue,
        desiredValue,
        std::memory_order_release,
        std::memory_order_relaxed))
    {
        RefCountPair const prev{curValue};
        if (!prev.strong)
            return false;

        desiredValue = curValue + strongDelta;
    }
    return true;
}

inline bool
IntrusiveRefCounts::expired() const noexcept
{
    RefCountPair const val = refCounts.load(std::memory_order_acquire);
    return val.strong == 0;
}

inline std::size_t
IntrusiveRefCounts::use_count() const noexcept
{
    RefCountPair const val = refCounts.load(std::memory_order_acquire);
    return val.strong;
}

inline IntrusiveRefCounts::~IntrusiveRefCounts() noexcept
{
    // TODO remove me:
#ifndef NDEBUG
    auto v = refCounts.load(std::memory_order_acquire);
    assert(!(v & valueMask));
    auto t = v & tagMask;
    assert(!t || t == tagMask);
#endif
}

//------------------------------------------------------------------------------

inline IntrusiveRefCounts::RefCountPair::RefCountPair(std::uint32_t v) noexcept
    : strong{v & strongMask}
    , weak{(v & weakMask) >> 16}
    , partialDestroyStartedBit{v & partialDestroyStartedMask}
    , partialDestroyFinishedBit{v & partialDestroyFinishedMask}
{
    assert(strong < checkStrongMaxValue && weak < checkWeakMaxValue);
}

inline IntrusiveRefCounts::RefCountPair::RefCountPair(
    std::uint32_t s,
    std::uint32_t w) noexcept
    : strong{s}, weak{w}
{
    assert(strong < checkStrongMaxValue && weak < checkWeakMaxValue);
}

inline std::uint32_t
IntrusiveRefCounts::RefCountPair::combinedValue() const noexcept
{
    assert(strong < checkStrongMaxValue && weak < checkWeakMaxValue);
    return (weak << 16) | strong | partialDestroyStartedBit |
        partialDestroyFinishedBit;
}

template <class T>
inline void
partialDestructorFinished(T** o)
{
    T& self = **o;
    IntrusiveRefCounts::RefCountPair p =
        self.refCounts.fetch_or(IntrusiveRefCounts::partialDestroyFinishedMask);
    assert(
        !p.partialDestroyFinishedBit && p.partialDestroyStartedBit &&
        !p.strong);
    if (!p.weak)
    {
        // There was a weak count before the partial destructor ran (or we would
        // have run the full destructor) and now there isn't a weak count. Some
        // thread is waiting to run the destructor.
        self.refCounts.notify_one();
    }
    // Set the pointer to null to emphasize that the object shouldn't be used
    // after calling this function as it may be destroyed in another thread.
    *o = nullptr;
}
//------------------------------------------------------------------------------

}  // namespace ripple
#endif
