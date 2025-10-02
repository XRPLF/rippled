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

#ifndef XRPL_BASICS_INTRUSIVEPOINTER_H_INCLUDED
#define XRPL_BASICS_INTRUSIVEPOINTER_H_INCLUDED

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace ripple {

//------------------------------------------------------------------------------

/** Tag to create an intrusive pointer from another intrusive pointer by using a
    static cast. This is useful to create an intrusive pointer to a derived
    class from an intrusive pointer to a base class.
*/
struct StaticCastTagSharedIntrusive
{
};

/** Tag to create an intrusive pointer from another intrusive pointer by using a
    dynamic cast. This is useful to create an intrusive pointer to a derived
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

//------------------------------------------------------------------------------
//

template <class T>
concept CAdoptTag = std::is_same_v<T, SharedIntrusiveAdoptIncrementStrongTag> ||
    std::is_same_v<T, SharedIntrusiveAdoptNoIncrementTag>;

//------------------------------------------------------------------------------

/** A shared intrusive pointer class that supports weak pointers.

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
template <class T>
class SharedIntrusive
{
public:
    SharedIntrusive() = default;

    template <CAdoptTag TAdoptTag>
    SharedIntrusive(T* p, TAdoptTag) noexcept;

    SharedIntrusive(SharedIntrusive const& rhs);

    template <class TT>
    // TODO: convertible_to isn't quite right. That include a static castable.
    // Find the right concept.
        requires std::convertible_to<TT*, T*>
    SharedIntrusive(SharedIntrusive<TT> const& rhs);

    SharedIntrusive(SharedIntrusive&& rhs);

    template <class TT>
        requires std::convertible_to<TT*, T*>
    SharedIntrusive(SharedIntrusive<TT>&& rhs);

    SharedIntrusive&
    operator=(SharedIntrusive const& rhs);

    bool
    operator!=(std::nullptr_t) const;

    bool
    operator==(std::nullptr_t) const;

    template <class TT>
        requires std::convertible_to<TT*, T*>
    SharedIntrusive&
    operator=(SharedIntrusive<TT> const& rhs);

    SharedIntrusive&
    operator=(SharedIntrusive&& rhs);

    template <class TT>
        requires std::convertible_to<TT*, T*>
    SharedIntrusive&
    operator=(SharedIntrusive<TT>&& rhs);

    /** Adopt the raw pointer. The strong reference may or may not be
        incremented, depending on the TAdoptTag
     */
    template <CAdoptTag TAdoptTag = SharedIntrusiveAdoptIncrementStrongTag>
    void
    adopt(T* p);

    ~SharedIntrusive();

    /** Create a new SharedIntrusive by statically casting the pointer
        controlled by the rhs param.
    */
    template <class TT>
    SharedIntrusive(
        StaticCastTagSharedIntrusive,
        SharedIntrusive<TT> const& rhs);

    /** Create a new SharedIntrusive by statically casting the pointer
       controlled by the rhs param.
    */
    template <class TT>
    SharedIntrusive(StaticCastTagSharedIntrusive, SharedIntrusive<TT>&& rhs);

    /** Create a new SharedIntrusive by dynamically casting the pointer
       controlled by the rhs param.
    */
    template <class TT>
    SharedIntrusive(
        DynamicCastTagSharedIntrusive,
        SharedIntrusive<TT> const& rhs);

    /** Create a new SharedIntrusive by dynamically casting the pointer
       controlled by the rhs param.
    */
    template <class TT>
    SharedIntrusive(DynamicCastTagSharedIntrusive, SharedIntrusive<TT>&& rhs);

    T&
    operator*() const noexcept;

    T*
    operator->() const noexcept;

    explicit
    operator bool() const noexcept;

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

    template <class TT, class... Args>
    friend SharedIntrusive<TT>
    make_SharedIntrusive(Args&&... args);

    template <class TT>
    friend class SharedIntrusive;

    template <class TT>
    friend class SharedWeakUnion;

    template <class TT>
    friend class WeakIntrusive;

private:
    /** Return the raw pointer held by this object. */
    T*
    unsafeGetRawPtr() const;

    /** Exchange the current raw pointer held by this object with the given
        pointer. Decrement the strong count of the raw pointer previously held
        by this object and run the appropriate release action.
     */
    void
    unsafeReleaseAndStore(T* next);

    /** Set the raw pointer directly. This is wrapped in a function so the class
        can support both atomic and non-atomic pointers in a future patch.
     */
    void
    unsafeSetRawPtr(T* p);

    /** Exchange the raw pointer directly.
        This sets the raw pointer to the given value and returns the previous
        value. This is wrapped in a function so the class can support both
        atomic and non-atomic pointers in a future patch.
     */
    T*
    unsafeExchange(T* p);

    /** pointer to the type with an intrusive count */
    T* ptr_{nullptr};
};

//------------------------------------------------------------------------------

/** A weak intrusive pointer class for the SharedIntrusive pointer class.

Note that this weak pointer class asks differently from normal weak pointer
classes. When the strong pointer count goes to zero, the "partialDestructor"
is called. See the comment on SharedIntrusive for a fuller explanation.
*/
template <class T>
class WeakIntrusive
{
public:
    WeakIntrusive() = default;

    WeakIntrusive(WeakIntrusive const& rhs);

    WeakIntrusive(WeakIntrusive&& rhs);

    WeakIntrusive(SharedIntrusive<T> const& rhs);

    // There is no move constructor from a strong intrusive ptr because
    // moving would be move expensive than copying in this case (the strong
    // ref would need to be decremented)
    WeakIntrusive(SharedIntrusive<T> const&& rhs) = delete;

    // Since there are no current use cases for copy assignment in
    // WeakIntrusive, we delete this operator to simplify the implementation. If
    // a need arises in the future, we can reintroduce it with proper
    // consideration."
    WeakIntrusive&
    operator=(WeakIntrusive const&) = delete;

    template <class TT>
        requires std::convertible_to<TT*, T*>
    WeakIntrusive&
    operator=(SharedIntrusive<TT> const& rhs);

    /** Adopt the raw pointer and increment the weak count. */
    void
    adopt(T* ptr);

    ~WeakIntrusive();

    /** Get a strong pointer from the weak pointer, if possible. This will
       only return a seated pointer if the strong count on the raw pointer
       is non-zero before locking.
     */
    SharedIntrusive<T>
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

    /** Decrement the weak count. This does _not_ set the raw pointer to
    null.

    Note: This may run the destructor if the strong count is zero.
    */
    void
    unsafeReleaseNoStore();
};

//------------------------------------------------------------------------------

/** A combination of a strong and a weak intrusive pointer stored in the
    space of a single pointer.

    This class is similar to a `std::variant<SharedIntrusive,WeakIntrusive>`
    with some optimizations. In particular, it uses a low-order bit to
    determine if the raw pointer represents a strong pointer or a weak
    pointer. It can also be quickly switched between its strong pointer and
    weak pointer representations. This class is useful for storing intrusive
    pointers in tagged caches.
  */

template <class T>
class SharedWeakUnion
{
    // Tagged pointer. Low bit determines if this is a strong or a weak
    // pointer. The low bit must be masked to zero when converting back to a
    // pointer. If the low bit is '1', this is a weak pointer.
    static_assert(
        alignof(T) >= 2,
        "Bad alignment: Combo pointer requires low bit to be zero");

public:
    SharedWeakUnion() = default;

    SharedWeakUnion(SharedWeakUnion const& rhs);

    template <class TT>
        requires std::convertible_to<TT*, T*>
    SharedWeakUnion(SharedIntrusive<TT> const& rhs);

    SharedWeakUnion(SharedWeakUnion&& rhs);

    template <class TT>
        requires std::convertible_to<TT*, T*>
    SharedWeakUnion(SharedIntrusive<TT>&& rhs);

    SharedWeakUnion&
    operator=(SharedWeakUnion const& rhs);

    template <class TT>
        requires std::convertible_to<TT*, T*>
    SharedWeakUnion&
    operator=(SharedIntrusive<TT> const& rhs);

    template <class TT>
        requires std::convertible_to<TT*, T*>
    SharedWeakUnion&
    operator=(SharedIntrusive<TT>&& rhs);

    ~SharedWeakUnion();

    /** Return a strong pointer if this is already a strong pointer (i.e.
       don't lock the weak pointer. Use the `lock` method if that's what's
       needed)
     */
    SharedIntrusive<T>
    getStrong() const;

    /** Return true if this is a strong pointer and the strong pointer is
        seated.
     */
    explicit
    operator bool() const noexcept;

    /** Set the pointer to null, decrement the appropriate ref count, and
       run the appropriate release action.
     */
    void
    reset();

    /** If this is a strong pointer, return the raw pointer. Otherwise
       return null.
     */
    T*
    get() const;

    /** If this is a strong pointer, return the strong count. Otherwise
     * return 0
     */
    std::size_t
    use_count() const;

    /** Return true if there is a non-zero strong count. */
    bool
    expired() const;

    /** If this is a strong pointer, return the strong pointer. Otherwise
        attempt to lock the weak pointer.
     */
    SharedIntrusive<T>
    lock() const;

    /** Return true is this represents a strong pointer. */
    bool
    isStrong() const;

    /** Return true is this represents a weak pointer. */
    bool
    isWeak() const;

    /** If this is a weak pointer, attempt to convert it to a strong
       pointer.

        @return true if successfully converted to a strong pointer (or was
                already a strong pointer). Otherwise false.
      */
    bool
    convertToStrong();

    /** If this is a strong pointer, attempt to convert it to a weak
       pointer.

        @return false if the pointer is null. Otherwise return true.
      */
    bool
    convertToWeak();

private:
    // Tagged pointer. Low bit determines if this is a strong or a weak
    // pointer. The low bit must be masked to zero when converting back to a
    // pointer. If the low bit is '1', this is a weak pointer.
    std::uintptr_t tp_{0};
    static constexpr std::uintptr_t tagMask = 1;
    static constexpr std::uintptr_t ptrMask = ~tagMask;

private:
    /** Return the raw pointer held by this object.
     */
    T*
    unsafeGetRawPtr() const;

    enum class RefStrength { strong, weak };
    /** Set the raw pointer and tag bit directly.
     */
    void
    unsafeSetRawPtr(T* p, RefStrength rs);

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

/** Create a shared intrusive pointer.

    Note: unlike std::shared_ptr, where there is an advantage of allocating
    the pointer and control block together, there is no benefit for intrusive
    pointers.
*/
template <class TT, class... Args>
SharedIntrusive<TT>
make_SharedIntrusive(Args&&... args)
{
    auto p = new TT(std::forward<Args>(args)...);

    static_assert(
        noexcept(SharedIntrusive<TT>(
            std::declval<TT*>(),
            std::declval<SharedIntrusiveAdoptNoIncrementTag>())),
        "SharedIntrusive constructor should not throw or this can leak "
        "memory");

    return SharedIntrusive<TT>(p, SharedIntrusiveAdoptNoIncrementTag{});
}

//------------------------------------------------------------------------------

namespace intr_ptr {
template <class T>
using SharedPtr = SharedIntrusive<T>;

template <class T>
using WeakPtr = WeakIntrusive<T>;

template <class T>
using SharedWeakUnionPtr = SharedWeakUnion<T>;

template <class T, class... A>
SharedPtr<T>
make_shared(A&&... args)
{
    return make_SharedIntrusive<T>(std::forward<A>(args)...);
}

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
