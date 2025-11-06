#ifndef XRPL_BASICS_INTRUSIVEREFCOUNTS_H_INCLUDED
#define XRPL_BASICS_INTRUSIVEREFCOUNTS_H_INCLUDED

#include <xrpl/beast/utility/instrumentation.h>

#include <atomic>
#include <cstdint>

namespace ripple {

/** Action to perform when releasing a strong pointer.

    noop: Do nothing. For example, a `noop` action will occur when a count is
    decremented to a non-zero value.

    partialDestroy: Run the `partialDestructor`. This action will happen when a
    strong count is decremented to zero and the weak count is non-zero.

    destroy: Run the destructor. This action will occur when either the strong
    count or weak count is decremented and the other count is also zero.
 */
enum class ReleaseStrongRefAction { noop, partialDestroy, destroy };

/** Action to perform when releasing a weak pointer.

    noop: Do nothing. For example, a `noop` action will occur when a count is
    decremented to a non-zero value.

    destroy: Run the destructor. This action will occur when either the strong
    count or weak count is decremented and the other count is also zero.
 */
enum class ReleaseWeakRefAction { noop, destroy };

/** Implement the strong count, weak count, and bit flags for an intrusive
    pointer.

    A class can satisfy the requirements of a ripple::IntrusivePointer by
    inheriting from this class.
  */
struct IntrusiveRefCounts
{
    virtual ~IntrusiveRefCounts() noexcept;

    // This must be `noexcept` or the make_SharedIntrusive function could leak
    // memory.
    void
    addStrongRef() const noexcept;

    void
    addWeakRef() const noexcept;

    ReleaseStrongRefAction
    releaseStrongRef() const;

    // Same as:
    // {
    //   addWeakRef();
    //   return releaseStrongRef;
    // }
    // done as one atomic operation
    ReleaseStrongRefAction
    addWeakReleaseStrongRef() const;

    ReleaseWeakRefAction
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
    // TODO: We may need to use a uint64_t for both counts. This will reduce the
    // memory savings. We need to audit the code to make sure 16 bit counts are
    // enough for strong pointers and 14 bit counts are enough for weak
    // pointers. Use type aliases to make it easy to switch types.
    using CountType = std::uint16_t;
    static constexpr size_t StrongCountNumBits = sizeof(CountType) * 8;
    static constexpr size_t WeakCountNumBits = StrongCountNumBits - 2;
    using FieldType = std::uint32_t;
    static constexpr size_t FieldTypeBits = sizeof(FieldType) * 8;
    static constexpr FieldType one = 1;

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

    mutable std::atomic<FieldType> refCounts{strongDelta};

    /**  Amount to change the strong count when adding or releasing a reference

         Note: The strong count is stored in the low `StrongCountNumBits` bits
       of refCounts
      */
    static constexpr FieldType strongDelta = 1;

    /**  Amount to change the weak count when adding or releasing a reference

         Note: The weak count is stored in the high `WeakCountNumBits` bits of
         refCounts
      */
    static constexpr FieldType weakDelta = (one << StrongCountNumBits);

    /**  Flag that is set when the partialDestroy function has started running
         (or is about to start running).

         See description of the `refCounts` field for a fuller description of
         this field.
      */
    static constexpr FieldType partialDestroyStartedMask =
        (one << (FieldTypeBits - 1));

    /**  Flag that is set when the partialDestroy function has finished running

         See description of the `refCounts` field for a fuller description of
         this field.
      */
    static constexpr FieldType partialDestroyFinishedMask =
        (one << (FieldTypeBits - 2));

    /** Mask that will zero out all the `count` bits and leave the tag bits
        unchanged.
      */
    static constexpr FieldType tagMask =
        partialDestroyStartedMask | partialDestroyFinishedMask;

    /** Mask that will zero out the `tag` bits and leave the count bits
        unchanged.
      */
    static constexpr FieldType valueMask = ~tagMask;

    /** Mask that will zero out everything except the strong count.
     */
    static constexpr FieldType strongMask =
        ((one << StrongCountNumBits) - 1) & valueMask;

    /** Mask that will zero out everything except the weak count.
     */
    static constexpr FieldType weakMask =
        (((one << WeakCountNumBits) - 1) << StrongCountNumBits) & valueMask;

    /** Unpack the count and tag fields from the packed atomic integer form. */
    struct RefCountPair
    {
        CountType strong;
        CountType weak;
        /**  The `partialDestroyStartedBit` is set to on when the partial
             destroy function is started. It is not a boolean; it is a uint32
             with all bits zero with the possible exception of the
             `partialDestroyStartedMask` bit. This is done so it can be directly
             masked into the `combinedValue`.
         */
        FieldType partialDestroyStartedBit{0};
        /**  The `partialDestroyFinishedBit` is set to on when the partial
             destroy function has finished.
         */
        FieldType partialDestroyFinishedBit{0};
        RefCountPair(FieldType v) noexcept;
        RefCountPair(CountType s, CountType w) noexcept;

        /** Convert back to the packed integer form. */
        FieldType
        combinedValue() const noexcept;

        static constexpr CountType maxStrongValue =
            static_cast<CountType>((one << StrongCountNumBits) - 1);
        static constexpr CountType maxWeakValue =
            static_cast<CountType>((one << WeakCountNumBits) - 1);
        /**  Put an extra margin to detect when running up against limits.
             This is only used in debug code, and is useful if we reduce the
             number of bits in the strong and weak counts (to 16 and 14 bits).
         */
        static constexpr CountType checkStrongMaxValue = maxStrongValue - 32;
        static constexpr CountType checkWeakMaxValue = maxWeakValue - 32;
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

inline ReleaseStrongRefAction
IntrusiveRefCounts::releaseStrongRef() const
{
    // Subtract `strongDelta` from refCounts. If this releases the last strong
    // ref, set the `partialDestroyStarted` bit. It is important that the ref
    // count and the `partialDestroyStartedBit` are changed atomically (hence
    // the loop and `compare_exchange` op). If this didn't need to be done
    // atomically, the loop could be replaced with a `fetch_sub` and a
    // conditional `fetch_or`. This loop will almost always run once.

    using enum ReleaseStrongRefAction;
    auto prevIntVal = refCounts.load(std::memory_order_acquire);
    while (1)
    {
        RefCountPair const prevVal{prevIntVal};
        XRPL_ASSERT(
            (prevVal.strong >= strongDelta),
            "ripple::IntrusiveRefCounts::releaseStrongRef : previous ref "
            "higher than new");
        auto nextIntVal = prevIntVal - strongDelta;
        ReleaseStrongRefAction action = noop;
        if (prevVal.strong == 1)
        {
            if (prevVal.weak == 0)
            {
                action = destroy;
            }
            else
            {
                nextIntVal |= partialDestroyStartedMask;
                action = partialDestroy;
            }
        }

        if (refCounts.compare_exchange_weak(
                prevIntVal, nextIntVal, std::memory_order_acq_rel))
        {
            // Can't be in partial destroy because only decrementing the strong
            // count to zero can start a partial destroy, and that can't happen
            // twice.
            XRPL_ASSERT(
                (action == noop) || !(prevIntVal & partialDestroyStartedMask),
                "ripple::IntrusiveRefCounts::releaseStrongRef : not in partial "
                "destroy");
            return action;
        }
    }
}

inline ReleaseStrongRefAction
IntrusiveRefCounts::addWeakReleaseStrongRef() const
{
    using enum ReleaseStrongRefAction;

    static_assert(weakDelta > strongDelta);
    auto constexpr delta = weakDelta - strongDelta;
    auto prevIntVal = refCounts.load(std::memory_order_acquire);
    // This loop will almost always run once. The loop is needed to atomically
    // change the counts and flags (the count could be atomically changed, but
    // the flags depend on the current value of the counts).
    //
    // Note: If this becomes a perf bottleneck, the `partialDestoryStartedMask`
    // may be able to be set non-atomically. But it is easier to reason about
    // the code if the flag is set atomically.
    while (1)
    {
        RefCountPair const prevVal{prevIntVal};
        // Converted the last strong pointer to a weak pointer.
        //
        // Can't be in partial destroy because only decrementing the
        // strong count to zero can start a partial destroy, and that
        // can't happen twice.
        XRPL_ASSERT(
            (!prevVal.partialDestroyStartedBit),
            "ripple::IntrusiveRefCounts::addWeakReleaseStrongRef : not in "
            "partial destroy");

        auto nextIntVal = prevIntVal + delta;
        ReleaseStrongRefAction action = noop;
        if (prevVal.strong == 1)
        {
            if (prevVal.weak == 0)
            {
                action = noop;
            }
            else
            {
                nextIntVal |= partialDestroyStartedMask;
                action = partialDestroy;
            }
        }
        if (refCounts.compare_exchange_weak(
                prevIntVal, nextIntVal, std::memory_order_acq_rel))
        {
            XRPL_ASSERT(
                (!(prevIntVal & partialDestroyStartedMask)),
                "ripple::IntrusiveRefCounts::addWeakReleaseStrongRef : not "
                "started partial destroy");
            return action;
        }
    }
}

inline ReleaseWeakRefAction
IntrusiveRefCounts::releaseWeakRef() const
{
    auto prevIntVal = refCounts.fetch_sub(weakDelta, std::memory_order_acq_rel);
    RefCountPair prev = prevIntVal;
    if (prev.weak == 1 && prev.strong == 0)
    {
        if (!prev.partialDestroyStartedBit)
        {
            // This case should only be hit if the partialDestroyStartedBit is
            // set non-atomically (and even then very rarely). The code is kept
            // in case we need to set the flag non-atomically for perf reasons.
            refCounts.wait(prevIntVal, std::memory_order_acquire);
            prevIntVal = refCounts.load(std::memory_order_acquire);
            prev = RefCountPair{prevIntVal};
        }
        if (!prev.partialDestroyFinishedBit)
        {
            // partial destroy MUST finish before running a full destroy (when
            // using weak pointers)
            refCounts.wait(prevIntVal - weakDelta, std::memory_order_acquire);
        }
        return ReleaseWeakRefAction::destroy;
    }
    return ReleaseWeakRefAction::noop;
}

inline bool
IntrusiveRefCounts::checkoutStrongRefFromWeak() const noexcept
{
    auto curValue = RefCountPair{1, 1}.combinedValue();
    auto desiredValue = RefCountPair{2, 1}.combinedValue();

    while (!refCounts.compare_exchange_weak(
        curValue, desiredValue, std::memory_order_acq_rel))
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
#ifndef NDEBUG
    auto v = refCounts.load(std::memory_order_acquire);
    XRPL_ASSERT(
        (!(v & valueMask)),
        "ripple::IntrusiveRefCounts::~IntrusiveRefCounts : count must be zero");
    auto t = v & tagMask;
    XRPL_ASSERT(
        (!t || t == tagMask),
        "ripple::IntrusiveRefCounts::~IntrusiveRefCounts : valid tag");
#endif
}

//------------------------------------------------------------------------------

inline IntrusiveRefCounts::RefCountPair::RefCountPair(
    IntrusiveRefCounts::FieldType v) noexcept
    : strong{static_cast<CountType>(v & strongMask)}
    , weak{static_cast<CountType>((v & weakMask) >> StrongCountNumBits)}
    , partialDestroyStartedBit{v & partialDestroyStartedMask}
    , partialDestroyFinishedBit{v & partialDestroyFinishedMask}
{
    XRPL_ASSERT(
        (strong < checkStrongMaxValue && weak < checkWeakMaxValue),
        "ripple::IntrusiveRefCounts::RefCountPair(FieldType) : inputs inside "
        "range");
}

inline IntrusiveRefCounts::RefCountPair::RefCountPair(
    IntrusiveRefCounts::CountType s,
    IntrusiveRefCounts::CountType w) noexcept
    : strong{s}, weak{w}
{
    XRPL_ASSERT(
        (strong < checkStrongMaxValue && weak < checkWeakMaxValue),
        "ripple::IntrusiveRefCounts::RefCountPair(CountType, CountType) : "
        "inputs inside range");
}

inline IntrusiveRefCounts::FieldType
IntrusiveRefCounts::RefCountPair::combinedValue() const noexcept
{
    XRPL_ASSERT(
        (strong < checkStrongMaxValue && weak < checkWeakMaxValue),
        "ripple::IntrusiveRefCounts::RefCountPair::combinedValue : inputs "
        "inside range");
    return (static_cast<IntrusiveRefCounts::FieldType>(weak)
            << IntrusiveRefCounts::StrongCountNumBits) |
        static_cast<IntrusiveRefCounts::FieldType>(strong) |
        partialDestroyStartedBit | partialDestroyFinishedBit;
}

template <class T>
inline void
partialDestructorFinished(T** o)
{
    T& self = **o;
    IntrusiveRefCounts::RefCountPair p =
        self.refCounts.fetch_or(IntrusiveRefCounts::partialDestroyFinishedMask);
    XRPL_ASSERT(
        (!p.partialDestroyFinishedBit && p.partialDestroyStartedBit &&
         !p.strong),
        "ripple::partialDestructorFinished : not a weak ref");
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
