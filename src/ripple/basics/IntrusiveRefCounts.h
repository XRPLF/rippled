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

// TODO move this file to basics
//
namespace ripple {

enum class ReleaseRefAction { noop, partialDestroy, destroy };

// This can be embedded in classes to implement the two atomic counts
struct IntrusiveRefCounts
{
    virtual ~IntrusiveRefCounts();

    // TODO The `const` is weird, but needed because this is for an intrusive
    // pointer type
    void
    addStrongRef() const;

    void
    addWeakRef() const;

    ReleaseRefAction
    releaseStrongRef() const;

    ReleaseRefAction
    releaseWeakRef() const;

    // Returns true is able to checkout a strong ref. False otherwise
    bool
    checkoutStrongRefFromWeak() const;

    bool
    expired() const;

    std::size_t
    use_count() const;

    // TODO: document why this isn't a member function and why it takes a
    // pointer
    // TODO: Rename to indicate o may be destroyed after this function runs
    friend void
    partialDestructorFinished(IntrusiveRefCounts* o);

private:
    // TODO: Check what expected counts are. Probally more bits for strong
    // low 16 bits is strong count; high 16 bits is the weak count
    // They need to be treated atomicly together
    mutable std::atomic<std::uint32_t> refCounts{0};
    static constexpr std::uint32_t strongDelta = 1;
    static constexpr std::uint32_t weakDelta = (1 << 16);
    // TODO: Document this
    static constexpr std::uint32_t partialDestroyStartedMask = (1 << 31);
    static constexpr std::uint32_t partialDestroyFinishedMask = (1 << 30);
    static constexpr std::uint32_t tagMask =
        partialDestroyStartedMask | partialDestroyFinishedMask;
    static constexpr std::uint32_t valueMask = ~tagMask;
    static constexpr std::uint32_t strongMask = 0xffff;
    static constexpr std::uint32_t weakMask = 0xffff0000 & valueMask;

    struct RefCountPair
    {
        std::uint32_t strong;
        std::uint32_t weak;
        // TODO: Document. In particular, these are not boolean, but can be
        // directly masked into a result when computing the combined value
        std::uint32_t partialDestroyStartedBit{0};
        std::uint32_t partialDestroyFinishedBit{0};
        RefCountPair(std::uint32_t v);
        RefCountPair(std::uint32_t s, std::uint32_t w);

        std::uint32_t
        combinedValue() const;

        static constexpr std::uint32_t maxValue = (1 << 16) - 1;
        // Put an extra margin to detect when running up against limits
        static constexpr std::uint32_t checkMaxValue = maxValue - 32;
    };
};

inline void
IntrusiveRefCounts::addStrongRef() const
{
    refCounts.fetch_add(strongDelta, std::memory_order_acq_rel);
}

inline void
IntrusiveRefCounts::addWeakRef() const
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
            // count to zero can start a partial destory, and that can't happen
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
IntrusiveRefCounts::checkoutStrongRefFromWeak() const
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
IntrusiveRefCounts::expired() const
{
    RefCountPair const val = refCounts.load(std::memory_order_acquire);
    return val.strong == 0;
}

inline std::size_t
IntrusiveRefCounts::use_count() const
{
    RefCountPair const val = refCounts.load(std::memory_order_acquire);
    return val.strong;
}

inline IntrusiveRefCounts::~IntrusiveRefCounts()
{
    // TODO remove me:
#ifndef NDEBUG
    auto v = refCounts.load(std::memory_order_acquire);
    assert(!(v & valueMask));
    auto t = v & tagMask;
    assert(!t || t == tagMask);
#endif
}

inline IntrusiveRefCounts::RefCountPair::RefCountPair(std::uint32_t v)
    : strong{v & strongMask}
    , weak{(v & weakMask) >> 16}
    , partialDestroyStartedBit{v & partialDestroyStartedMask}
    , partialDestroyFinishedBit{v & partialDestroyFinishedMask}
{
}

inline IntrusiveRefCounts::RefCountPair::RefCountPair(
    std::uint32_t s,
    std::uint32_t w)
    : strong{s}, weak{w}
{
    assert(strong < checkMaxValue && weak < checkMaxValue);
}

inline std::uint32_t
IntrusiveRefCounts::RefCountPair::combinedValue() const
{
    assert(strong < checkMaxValue && weak < checkMaxValue);
    return (weak << 16) | strong | partialDestroyStartedBit |
        partialDestroyFinishedBit;
}

inline void
partialDestructorFinished(IntrusiveRefCounts* o)
{
    IntrusiveRefCounts::RefCountPair p =
        o->refCounts.fetch_or(IntrusiveRefCounts::partialDestroyFinishedMask);
    assert(
        !p.partialDestroyFinishedBit && p.partialDestroyStartedBit &&
        !p.strong);
    if (!p.weak)
    {
        // There was a weak count before the partial destructor ran (or we would
        // have run the full destructor) and now there isn't a weak count. Some
        // thread is waiting to run the destructor.
        o->refCounts.notify_one();
    }
}
//------------------------------------------------------------------------------

}  // namespace ripple
#endif
