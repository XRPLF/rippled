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

#ifndef RIPPLE_BASICS_INTRUSIVEPOINTER_H_INCLUDED
#define RIPPLE_BASICS_INTRUSIVEPOINTER_H_INCLUDED

// shared pointer class for tree pointers
// The ref counts are kept on the tree pointers themselves
// I.e. this is an intrusive pointer type.

#include <atomic>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <type_traits>

// TODO: Remove me
// This is a temporary directive to enable or disable lockless inner nodes. The
// mutexes on the inner nodes can (likely) go away, but we need to audit the
// code carefully before we enable that.
#define SWD_LOCKLESS_INNER_NODE

namespace ripple {

// TODO re-add concept
// Concept is not working with inheritance. Disable for now.
#if 0
// clang-format off
template <typename T>
concept SharedIntrusiveRefCounted = requires(T a) {
    { a.addStrongRef() } -> std::same_as<void>;
    { a.releaseStrongRef() } -> std::same_as<ReleaseRefAction>;
    { a.addWeakRef() } -> std::same_as<void>;
    { a.releaseWeakRef() } -> std::same_as<ReleaseRefAction>;
    // When a strong pointer ref goes to zero, if there are any weak pointers
    // checked out a partial destructor is run, otherwise the regular destructor is run.
    { a.partialDestructor() } -> std::same_as<void>;
    {a.checkoutStrongRefFromWeak()} -> std::same_as<bool>;
    {a.use_count()} -> std::same_as<std::size_t>;
};
// clang-format on
#else
#define SharedIntrusiveRefCounted class
#endif

//------------------------------------------------------------------------------

/** Tag to create an intrusive pointer from another intrusive pointer by using a
    static cast. This is useful to create an intrusive pointer to a derived
    class from an intrusive pointer to a base class.
*/
struct StaticCastTagSharedIntrusive
{
};

/** Tag to create an intrusive pointer from another intrusive pointer by using a
    static cast. This is useful to create an intrusive pointer to a derived
    class from an intrusive pointer to a base class. If the cast fails an empty
    (null) intrusive pointer is created.
*/
struct DynamicCastTagSharedIntrusive
{
};

/** When creating or adopting a raw pointer, controls whether the strong count
    is incremented or not. Use this tag to increment the strong count.
*/
struct SharedIntrusiveAdoptIncrementStrongTag
{
};

/** When creating or adopting a raw pointer, controls whether the strong count
    is incremented or not. Use this tag to leave the strong count unchanged.
*/
struct SharedIntrusiveAdoptNoIncrementTag
{
};

/** A shared intrusive pointer class that supports weak pointers and optional
    atomic operations.

    This is meant to be used for SHAMapInnerNodes, but may be useful for other
    cases. Since the reference counts are stored on the pointee, the pointee is
    not destroyed until both the strong _and_ weak pointer counts go to zero.
    When the strong pointer count goes to zero, the "partialDestructor" is
    called. This can be used to destroy as much of the object as possible while
    still retaining the reference counts. For example, for SHAMapInnerNodes the
    children may be reset in that function. Note that std::shared_poiner WILL
    run the destructor when the strong count reaches zero, but may not free the
    memory used by the object until the weak count reaches zero. In rippled, we
    typically allocate shared pointers with the `make_shared` function. When
    that is used, the memory is not reclaimed until the weak count reaches zero.
*/
template <SharedIntrusiveRefCounted T, bool MakeAtomic>
class SharedIntrusive
{
public:
    SharedIntrusive() = default;

    template <class TAdoptTag>
        SharedIntrusive(T* p, TAdoptTag) noexcept
        // clang-format off
        requires std::is_same_v<TAdoptTag, SharedIntrusiveAdoptIncrementStrongTag> ||
                 std::is_same_v<TAdoptTag, SharedIntrusiveAdoptNoIncrementTag>;

    SharedIntrusive(SharedIntrusive const& rhs);

    template <class TT, bool IsAtomic>
    requires std::convertible_to<TT*, T*>
    SharedIntrusive(SharedIntrusive<TT, IsAtomic> const& rhs);

    SharedIntrusive(SharedIntrusive&& rhs);

    template <class TT, bool IsAtomic>
    requires std::convertible_to<TT*, T*>
    SharedIntrusive(SharedIntrusive<TT, IsAtomic>&& rhs);

    SharedIntrusive&
    operator=(SharedIntrusive const& rhs);

    template <class TT, bool IsAtomic>
    requires std::convertible_to<TT*, T*> SharedIntrusive&
    operator=(SharedIntrusive<TT, IsAtomic> const& rhs);

    SharedIntrusive&
    operator=(SharedIntrusive&& rhs);

    template <class TT, bool IsAtomic>
    requires std::convertible_to<TT*, T*> SharedIntrusive&
    operator=(SharedIntrusive<TT, IsAtomic>&& rhs);

    /** Adopt the raw pointer. The strong reference may or may not be incremented, depending on the TAdoptTag
    */
    template <class TAdoptTag>
        void
        adopt(T* p)
        // clang-format off
        requires std::is_same_v<TAdoptTag, SharedIntrusiveAdoptIncrementStrongTag> ||
                 std::is_same_v<TAdoptTag, SharedIntrusiveAdoptNoIncrementTag>;
    // clang-format on
    void
    adopt(T* p);

    ~SharedIntrusive();

    /** Create a new SharedIntrusive by statically casting the pointer
       controlled by the rhs param.
    */
    template <SharedIntrusiveRefCounted TT, bool IsAtomic>
    SharedIntrusive(
        StaticCastTagSharedIntrusive,
        SharedIntrusive<TT, IsAtomic> const& rhs);

    /** Create a new SharedIntrusive by statically casting the pointer
       controlled by the rhs param.
    */
    template <SharedIntrusiveRefCounted TT, bool IsAtomic>
    SharedIntrusive(
        StaticCastTagSharedIntrusive,
        SharedIntrusive<TT, IsAtomic>&& rhs);

    /** Create a new SharedIntrusive by dynamically casting the pointer
       controlled by the rhs param.
    */
    template <SharedIntrusiveRefCounted TT, bool IsAtomic>
    SharedIntrusive(
        DynamicCastTagSharedIntrusive,
        SharedIntrusive<TT, IsAtomic> const& rhs);

    /** Create a new SharedIntrusive by dynamically casting the pointer
       controlled by the rhs param.
    */
    template <SharedIntrusiveRefCounted TT, bool IsAtomic>
    SharedIntrusive(
        DynamicCastTagSharedIntrusive,
        SharedIntrusive<TT, IsAtomic>&& rhs);

    T&
    operator*() const noexcept;

    T*
    operator->() const noexcept;

    explicit operator bool() const noexcept;

    /** Set the pointer to null, decrement the strong count, and run the
        appropriate release action.
      */
    void
    reset();

    /** Get the raw pointer */
    T*
    get() const;

    /** Return the strong count */
    std::size_t
    use_count() const;

    template <SharedIntrusiveRefCounted TT, bool IsAtomic, class... Args>
    friend SharedIntrusive<TT, IsAtomic>
    make_SharedIntrusive(Args&&... args);

    // TODO: Make these "unsafe" functions private, if possible.
    // Note: Normally these "unsafe" function would be private. They are
    // currently public because C++ cannot make generic template classes
    // "friends" and other workarounds are uglier than making them public.

    /** Return the raw pointer held by this object.
     */
    T*
    unsafeGetRawPtr() const;

    /** Decrement the strong count of the raw pointer held by this object and
        run the appropriate release action. Note: this does _not_ set the raw
        pointer to null.
     */
    void
    unsafeReleaseNoStore();

    /** Set the raw pointer directly. This is wrapped in a function so the class
        can support both atomic and non-atomic pointers.
     */
    void
    unsafeSetRawPtr(T* p);

private:
    using PointerType = std::conditional_t<MakeAtomic, std::atomic<T*>, T*>;
    PointerType ptr_{nullptr};
};

//------------------------------------------------------------------------------

/** A weak intrusive pointer class for the SharedIntrusive pointer class.

    Note that this weak pointer class asks differently from normal weak pointer
    classes. When the strong pointer count goes to zero, the "partialDestructor"
    is called. See the comment on SharedIntrusive for a fuller explanation.
*/
template <SharedIntrusiveRefCounted T>
class WeakIntrusive
{
public:
    WeakIntrusive() = default;

    WeakIntrusive(WeakIntrusive const& rhs);

    WeakIntrusive(WeakIntrusive&& rhs);

    template <bool IsAtomic>
    WeakIntrusive(SharedIntrusive<T, IsAtomic> const& rhs);

    // There is no move constructor from a strong intrusive ptr because moving
    // would be move expensive than copying in this case (the strong ref would
    // need to be decremented)
    template <bool IsAtomic>
    WeakIntrusive(SharedIntrusive<T, IsAtomic> const&& rhs) = delete;

    template <class TT, bool IsAtomic>
    requires std::convertible_to<TT*, T*> WeakIntrusive&
    operator=(SharedIntrusive<TT, IsAtomic> const& rhs);

    /** Adopt the raw pointer and increment the weak count. */
    void
    adopt(T* ptr);

    ~WeakIntrusive();

    /** Get a strong pointer from the weak pointer, if possible. This will only
        return a seated pointer if the strong count on the raw pointer is
        non-zero before locking.
     */
    SharedIntrusive<T, false>
    lock() const;

    /** Return true if the strong count is zero. */
    bool
    expired() const;

    /** Set the pointer to null and decrement the weak count.

    Note: This may run the destructor if the strong count is zero.
    */
    void
    reset();

private:
    T* ptr_ = nullptr;

    /** Decrement the weak count. This does _not_ set the raw pointer to null.

    Note: This may run the destructor if the strong count is zero.
    */
    void
    unsafeReleaseNoStore();
};

//------------------------------------------------------------------------------

/** A combination of a strong and a weak intrusive pointer stored in the space
   of a single pointer.

    This class is similar to a `std::variant<SharedIntrusive,WeakIntrusive>`
    with some optimizations. In particular, it uses a low-order bit to determine
    if the raw pointer represents a strong pointer or a weak pointer. It can
    also be quickly switched between its strong pointer and weak pointer
    representations. This class is useful for storing intrusive pointers in
    tagged caches.
  */

// TODO Better name for this
template <SharedIntrusiveRefCounted T>
class SharedWeakUnion
{
    static_assert(
        alignof(T) >= 2,
        "Bad alignment: Combo pointer requires low bit to be zero");

public:
    SharedWeakUnion() = default;

    SharedWeakUnion(SharedWeakUnion const& rhs);

    template <class TT, bool IsAtomic>
    requires std::convertible_to<TT*, T*>
    SharedWeakUnion(SharedIntrusive<TT, IsAtomic> const& rhs);

    SharedWeakUnion(SharedWeakUnion&& rhs);

    template <class TT, bool IsAtomic>
    requires std::convertible_to<TT*, T*>
    SharedWeakUnion(SharedIntrusive<TT, IsAtomic>&& rhs);

    SharedWeakUnion&
    operator=(SharedWeakUnion const& rhs);

    template <class TT, bool IsAtomic>
    requires std::convertible_to<TT*, T*> SharedWeakUnion&
    operator=(SharedIntrusive<TT, IsAtomic> const& rhs);

    template <class TT, bool IsAtomic>
    requires std::convertible_to<TT*, T*> SharedWeakUnion&
    operator=(SharedIntrusive<TT, IsAtomic>&& rhs);

    ~SharedWeakUnion();

    /** Return a strong pointer if this is already a strong pointer (i.e. don't
        lock the weak pointer. Use the `lock` method if that's what's needed)
     */
    SharedIntrusive<T, false>
    getStrong() const;

    /** Return true if this is a strong pointer and the strong pointer is
        seated.
     */
    explicit operator bool() const noexcept;

    /** Set the pointer to null, decrement the appropriate ref count, and run
        the appropriate release action.
     */
    void
    reset();

    /** If this is a strong pointer, return the raw pointer. Otherwise return
        null.
     */
    T*
    get() const;

    /** If this is a strong pointer, return the strong count. Otherwise return 0
     */
    std::size_t
    use_count() const;

    /** Return true if there is a non-zero strong count. */
    bool
    expired() const;

    /** If this is a strong pointer, return the strong pointer. Otherwise
        attempt to lock the weak pointer.
     */
    SharedIntrusive<T, false>
    lock() const;

    /** Return true is this represents a strong pointer. */
    bool
    isStrong() const;

    /** Return true is this represents a weak pointer. */
    bool
    isWeak() const;

    /** If this is a weak pointer, attempt to convert it to a strong pointer.

        @return true if successfully converted to a strong pointer (or was
                already a strong pointer). Otherwise false.
      */
    bool
    convertToStrong();

    /** If this is a strong pointer, attempt to convert it to a weak pointer.

        @return false if the pointer is null. Otherwise return true.
      */
    bool
    convertToWeak();

private:
    // Tagged pointer. Low bit determines if this is a strong or a weak pointer.
    // The low bit must be masked to zero when converting back to a pointer.
    // If the low bit is '1', this is a weak pointer.
    std::uintptr_t tp_{0};
    static constexpr std::uintptr_t tagMask = 1;
    static constexpr std::uintptr_t ptrMask = ~tagMask;

private:
    /** Return the raw pointer held by this object.
     */
    T*
    unsafeGetRawPtr() const;

    /** Set the raw pointer and tag bit directly.
     */
    void
    unsafeSetRawPtr(T* p, bool isStrong);

    /** Set the raw pointer and tag bit to all zeros (strong null pointer).
     */
    void unsafeSetRawPtr(std::nullptr_t);

    /** Decrement the appropriate ref count, and run the appropriate release
        action. Note: this does _not_ set the raw pointer to null.
     */
    void
    unsafeReleaseNoStore();
};

//------------------------------------------------------------------------------

/** Create a (non-atomic) shared intrusive pointer.

    Note: unlike std::shared_ptr, where there is an advantage of allocating the
    pointer and control block together, there is no benefit for intrusive
    pointers.
*/
template <SharedIntrusiveRefCounted TT, bool IsAtomic, class... Args>
SharedIntrusive<TT, IsAtomic>
make_SharedIntrusive(Args&&... args)
{
    auto p = new TT(std::forward<Args>(args)...);

    static_assert(
        noexcept(SharedIntrusive<TT, IsAtomic>(
            std::declval<TT*>(),
            std::declval<SharedIntrusiveAdoptNoIncrementTag>())),
        "SharedIntrusive constructor should not throw or this can leak memory");

    return SharedIntrusive<TT, IsAtomic>(
        p, SharedIntrusiveAdoptNoIncrementTag{});
}

//------------------------------------------------------------------------------

namespace intr_ptr {
#ifdef SWD_LOCKLESS_INNER_NODE
template <SharedIntrusiveRefCounted T>
using MaybeAtomicSharedPtr = SharedIntrusive<T, /*atomic*/ true>;
#else
template <SharedIntrusiveRefCounted T>
using MaybeAtomicSharedPtr = SharedIntrusive<T, /*atomic*/ false>;
#endif

template <SharedIntrusiveRefCounted T>
using SharedPtr = SharedIntrusive<T, /*atomic*/ false>;

template <class T>
using WeakPtr = WeakIntrusive<T>;

template <class T, class... A>
MaybeAtomicSharedPtr<T>
make_maybe_atomic_shared(A&&... args)
{
#ifdef SWD_LOCKLESS_INNER_NODE
    return make_SharedIntrusive<T, true>(std::forward<A>(args)...);
#else
    return make_SharedIntrusive<T, false>(std::forward<A>(args)...);
#endif
}

template <class T, class... A>
SharedPtr<T>
make_shared(A&&... args)
{
    return make_SharedIntrusive<T, false>(std::forward<A>(args)...);
}

// TODO: Think about const
template <class T, class TT>
SharedPtr<T>
static_pointer_cast(TT const& v)
{
    return SharedPtr<T>(StaticCastTagSharedIntrusive{}, v);
}

template <class T, class TT>
SharedPtr<T>
dynamic_pointer_cast(TT const& v)
{
    return SharedPtr<T>(DynamicCastTagSharedIntrusive{}, v);
}
}  // namespace intr_ptr
}  // namespace ripple
#endif
