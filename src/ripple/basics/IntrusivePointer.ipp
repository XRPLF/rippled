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

#ifndef RIPPLE_BASICS_INTRUSIVEPOINTER_IPP_INCLUDED
#define RIPPLE_BASICS_INTRUSIVEPOINTER_IPP_INCLUDED

#include <ripple/basics/IntrusivePointer.h>

#include <ripple/basics/IntrusiveRefCounts.h>

namespace ripple {

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
    template <class TAdoptTag>
    SharedIntrusive<T, MakeAtomic>::SharedIntrusive(T* p, TAdoptTag) noexcept
    // clang-format off
        requires std::is_same_v<TAdoptTag, SharedIntrusiveAdoptIncrementStrongTag> ||
                 std::is_same_v<TAdoptTag, SharedIntrusiveAdoptNoIncrementTag>
    // clang-format on
    : ptr_{p}
{
    if constexpr (std::is_same_v<
                      TAdoptTag,
                      SharedIntrusiveAdoptIncrementStrongTag>)
    {
        if (p)
            p->addStrongRef();
    }
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
SharedIntrusive<T, MakeAtomic>::SharedIntrusive(SharedIntrusive const& rhs)
    : ptr_{[&] {
        auto p = rhs.unsafeGetRawPtr();
        if (p)
            p->addStrongRef();
        return p;
    }()}
{
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
template <class TT, bool IsAtomic>
requires std::convertible_to<TT*, T*>
SharedIntrusive<T, MakeAtomic>::SharedIntrusive(
    SharedIntrusive<TT, IsAtomic> const& rhs)
    : ptr_{[&] {
        auto p = rhs.unsafeGetRawPtr();
        if (p)
            p->addStrongRef();
        return p;
    }()}
{
}
template <SharedIntrusiveRefCounted T, bool MakeAtomic>
SharedIntrusive<T, MakeAtomic>::SharedIntrusive(SharedIntrusive&& rhs)
    : ptr_{rhs.unsafeGetRawPtr()}
{
    rhs.unsafeSetRawPtr(nullptr);
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
template <class TT, bool IsAtomic>
requires std::convertible_to<TT*, T*>
SharedIntrusive<T, MakeAtomic>::SharedIntrusive(
    SharedIntrusive<TT, IsAtomic>&& rhs)
    : ptr_{rhs.unsafeGetRawPtr()}
{
    rhs.unsafeSetRawPtr(nullptr);
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
SharedIntrusive<T, MakeAtomic>&
SharedIntrusive<T, MakeAtomic>::operator=(SharedIntrusive const& rhs)
{
    if (this == &rhs)
        return *this;
    unsafeReleaseNoStore();
    auto p = rhs.unsafeGetRawPtr();
    if (p)
        p->addStrongRef();
    unsafeSetRawPtr(p);
    return *this;
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
    template <class TT, bool IsAtomic, class TBypassAtomicBehaviorTag>
    SharedIntrusive<T, MakeAtomic>&
    SharedIntrusive<T, MakeAtomic>::assign(
        SharedIntrusive<TT, IsAtomic> const& rhs,
        TBypassAtomicBehaviorTag tag)
    // clang-format off
    requires std::convertible_to<TT*, T*> &&
        (std::is_same_v<TBypassAtomicBehaviorTag, SharedIntrusiveBypassAtomicOpsTag> ||
         std::is_same_v<TBypassAtomicBehaviorTag, SharedIntrusiveNormalAtomicOpsTag>)
// clang-format on
{
    if constexpr (std::is_same_v<T, TT> && IsAtomic == MakeAtomic)
    {
        if (this == &rhs)
            return *this;
    }
    unsafeReleaseNoStore();
    auto p = rhs.unsafeGetRawPtr();
    if (p)
        p->addStrongRef();
    unsafeSetRawPtr(p, tag);
    return *this;
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
template <class TT, bool IsAtomic>
// clang-format off
requires std::convertible_to<TT*, T*>
    // clang-format on
    SharedIntrusive<T, MakeAtomic>&
    SharedIntrusive<T, MakeAtomic>::operator=(
        SharedIntrusive<TT, IsAtomic> const& rhs)
{
    return assign(rhs, SharedIntrusiveNormalAtomicOpsTag{});
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
SharedIntrusive<T, MakeAtomic>&
SharedIntrusive<T, MakeAtomic>::operator=(SharedIntrusive&& rhs)
{
    if (this == &rhs)
        return *this;

    unsafeReleaseNoStore();
    unsafeSetRawPtr(rhs.unsafeGetRawPtr());
    rhs.unsafeSetRawPtr(nullptr);
    return *this;
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
    template <class TT, bool IsAtomic, class TBypassAtomicBehaviorTag>
    SharedIntrusive<T, MakeAtomic>&
    SharedIntrusive<T, MakeAtomic>::assign(
        SharedIntrusive<TT, IsAtomic>&& rhs,
        TBypassAtomicBehaviorTag tag)
    // clang-format off
    requires std::convertible_to<TT*, T*> &&
        (std::is_same_v<TBypassAtomicBehaviorTag, SharedIntrusiveBypassAtomicOpsTag> ||
         std::is_same_v<TBypassAtomicBehaviorTag, SharedIntrusiveNormalAtomicOpsTag>)
// clang-format on
{
    if constexpr (std::is_same_v<T, TT> && IsAtomic == MakeAtomic)
    {
        if (this == &rhs)
            return *this;
    }

    unsafeReleaseNoStore();
    unsafeSetRawPtr(rhs.unsafeGetRawPtr(), tag);
    rhs.unsafeSetRawPtr(nullptr);
    return *this;
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
template <class TT, bool IsAtomic>
requires std::convertible_to<TT*, T*> SharedIntrusive<T, MakeAtomic>&
SharedIntrusive<T, MakeAtomic>::operator=(SharedIntrusive<TT, IsAtomic>&& rhs)
{
    return assign(std::move(rhs), SharedIntrusiveNormalAtomicOpsTag{});
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
    template <class TAdoptTag>
    void
    SharedIntrusive<T, MakeAtomic>::adopt(T* p)
    // clang-format off
        requires std::is_same_v<TAdoptTag, SharedIntrusiveAdoptIncrementStrongTag> ||
                 std::is_same_v<TAdoptTag, SharedIntrusiveAdoptNoIncrementTag>
// clang-format on
{
    unsafeReleaseNoStore();
    if constexpr (std::is_same_v<
                      TAdoptTag,
                      SharedIntrusiveAdoptIncrementStrongTag>)
    {
        if (p)
            p->addStrongRef();
    }
    unsafeSetRawPtr(p);
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
void
SharedIntrusive<T, MakeAtomic>::adopt(T* p)
{
    unsafeReleaseNoStore();
    if (p)
        p->addStrongRef();
    unsafeSetRawPtr(p);
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
SharedIntrusive<T, MakeAtomic>::~SharedIntrusive()
{
    unsafeReleaseNoStore();
};

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
template <SharedIntrusiveRefCounted TT, bool IsAtomic>
SharedIntrusive<T, MakeAtomic>::SharedIntrusive(
    StaticCastTagSharedIntrusive,
    SharedIntrusive<TT, IsAtomic> const& rhs)
    : ptr_{[&] {
        auto p = static_cast<T*>(rhs.unsafeGetRawPtr());
        if (p)
            p->addStrongRef();
        return p;
    }()}
{
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
template <SharedIntrusiveRefCounted TT, bool IsAtomic>
SharedIntrusive<T, MakeAtomic>::SharedIntrusive(
    StaticCastTagSharedIntrusive,
    SharedIntrusive<TT, IsAtomic>&& rhs)
    : ptr_{static_cast<T*>(rhs.unsafeGetRawPtr())}
{
    rhs.unsafeSetRawPtr(nullptr);
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
template <SharedIntrusiveRefCounted TT, bool IsAtomic>
SharedIntrusive<T, MakeAtomic>::SharedIntrusive(
    DynamicCastTagSharedIntrusive,
    SharedIntrusive<TT, IsAtomic> const& rhs)
    : ptr_{[&] {
        auto p = dynamic_cast<T*>(rhs.unsafeGetRawPtr());
        if (p)
            p->addStrongRef();
        return p;
    }()}
{
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
template <SharedIntrusiveRefCounted TT, bool IsAtomic>
SharedIntrusive<T, MakeAtomic>::SharedIntrusive(
    DynamicCastTagSharedIntrusive,
    SharedIntrusive<TT, IsAtomic>&& rhs)
    : ptr_{dynamic_cast<T*>(rhs.unsafeGetRawPtr())}
{
    // Don't clear rhs pointer if dynamic_cast fails or will leak
    if (ptr_)
        rhs.unsafeSetRawPtr(nullptr);
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
T&
SharedIntrusive<T, MakeAtomic>::operator*() const noexcept
{
    return *unsafeGetRawPtr();
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
T*
SharedIntrusive<T, MakeAtomic>::operator->() const noexcept
{
    return unsafeGetRawPtr();
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
SharedIntrusive<T, MakeAtomic>::operator bool() const noexcept
{
    return bool(unsafeGetRawPtr());
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
void
SharedIntrusive<T, MakeAtomic>::reset()
{
    unsafeReleaseNoStore();
    unsafeSetRawPtr(nullptr);
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
void
SharedIntrusive<T, MakeAtomic>::reset(
    SharedIntrusiveBypassAtomicOpsTag bypassTag)
{
    unsafeReleaseNoStore();
    unsafeSetRawPtr(nullptr, bypassTag);
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
T*
SharedIntrusive<T, MakeAtomic>::get() const
{
    return unsafeGetRawPtr();
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
T*
SharedIntrusive<T, MakeAtomic>::get(SharedIntrusiveBypassAtomicOpsTag tag) const
{
    return unsafeGetRawPtr(tag);
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
std::size_t
SharedIntrusive<T, MakeAtomic>::use_count() const
{
    if (auto p = unsafeGetRawPtr())
        return p->use_count();
    return 0;
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
T*
SharedIntrusive<T, MakeAtomic>::unsafeGetRawPtr() const
{
    if constexpr (MakeAtomic)
    {
        // TODO: confirm atomic_ref's semantics will do what I want here.
        std::atomic_ref wrapped{ptr_};
        return wrapped.load(std::memory_order_acquire);
    }
    else
    {
        return ptr_;
    }
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
T* SharedIntrusive<T, MakeAtomic>::unsafeGetRawPtr(
    SharedIntrusiveBypassAtomicOpsTag) const
{
    return ptr_;
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
void
SharedIntrusive<T, MakeAtomic>::unsafeReleaseNoStore()
{
    auto p = unsafeGetRawPtr();
    if (!p)
        return;

    using enum ReleaseRefAction;
    auto action = p->releaseStrongRef();
    switch (action)
    {
        case noop:
            break;
        case destroy:
            delete p;
            break;
        case partialDestroy:
            p->partialDestructor();
            partialDestructorFinished(&p);
            // p is null and may no longer be used
            break;
    }
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
void
SharedIntrusive<T, MakeAtomic>::unsafeSetRawPtr(
    T* p,
    SharedIntrusiveNormalAtomicOpsTag)
{
    if constexpr (MakeAtomic)
    {
        std::atomic_ref wrapped{ptr_};
        wrapped.store(p, std::memory_order_release);
    }
    else
    {
        ptr_ = p;
    }
}

template <SharedIntrusiveRefCounted T, bool MakeAtomic>
void
SharedIntrusive<T, MakeAtomic>::unsafeSetRawPtr(
    T* p,
    SharedIntrusiveBypassAtomicOpsTag)
{
    ptr_ = p;
}

//------------------------------------------------------------------------------

template <SharedIntrusiveRefCounted T>
WeakIntrusive<T>::WeakIntrusive(WeakIntrusive const& rhs) : ptr_{rhs.ptr_}
{
    if (ptr_)
        ptr_->addWeakRef();
}

template <SharedIntrusiveRefCounted T>
WeakIntrusive<T>::WeakIntrusive(WeakIntrusive&& rhs) : ptr_{rhs.ptr_}
{
    rhs.ptr_ = nullptr;
}

template <SharedIntrusiveRefCounted T>
template <bool IsAtomic>
WeakIntrusive<T>::WeakIntrusive(SharedIntrusive<T, IsAtomic> const& rhs)
    : ptr_{rhs.unsafeGetRawPtr()}
{
    if (ptr_)
        ptr_->addWeakRef();
}

// Note: there is no move constructor from a strong intrusive ptr. Moving
// would be move expensive than copying in this case (the strong ref would
// need to be decremented)
// template <bool IsAtomic>
// WeakIntrusive(SharedIntrusive<T, IsAtomic> const&& rhs);

template <SharedIntrusiveRefCounted T>
template <class TT, bool IsAtomic>
requires std::convertible_to<TT*, T*> WeakIntrusive<T>&
WeakIntrusive<T>::operator=(SharedIntrusive<TT, IsAtomic> const& rhs)
{
    unsafeReleaseNoStore();
    auto p = rhs.unsafeGetRawPtr();
    if (p)
        p->addWeakRef();
    return *this;
}

template <SharedIntrusiveRefCounted T>
void
WeakIntrusive<T>::adopt(T* ptr)
{
    unsafeReleaseNoStore();
    if (ptr)
        ptr->addWeakRef();
    ptr_ = ptr;
}

template <SharedIntrusiveRefCounted T>
WeakIntrusive<T>::~WeakIntrusive()
{
    unsafeReleaseNoStore();
}

template <SharedIntrusiveRefCounted T>
SharedIntrusive<T, false>
WeakIntrusive<T>::lock() const
{
    if (ptr_ && ptr_->checkoutStrongRefFromWeak())
    {
        return SharedIntrusive<T, false>{
            ptr_, SharedIntrusiveAdoptNoIncrementTag{}};
    }
    return {};
}

template <SharedIntrusiveRefCounted T>
bool
WeakIntrusive<T>::expired() const
{
    return (!ptr_ || ptr_->expired());
}

template <SharedIntrusiveRefCounted T>
void
WeakIntrusive<T>::reset()
{
    if (!ptr_)
        return;

    unsafeReleaseNoStore();
    ptr_ = nullptr;
}

template <SharedIntrusiveRefCounted T>
void
WeakIntrusive<T>::unsafeReleaseNoStore()
{
    if (!ptr_)
        return;

    using enum ReleaseRefAction;
    auto action = ptr_->releaseWeakRef();
    switch (action)
    {
        case noop:
            break;
        case destroy:
            delete ptr_;
            break;
        case partialDestroy:
            assert(0);  // only a strong pointer should case a
                        // partialDestruction
            ptr_->partialDestructor();
            partialDestructorFinished(&ptr_);
            // ptr_ is null and may no longer be used
            break;
    }
}

//------------------------------------------------------------------------------

template <SharedIntrusiveRefCounted T>
SharedWeakUnion<T>::SharedWeakUnion(SharedWeakUnion const& rhs) : tp_{rhs.tp_}
{
    auto p = rhs.unsafeGetRawPtr();
    if (!p)
        return;

    if (rhs.isStrong())
        p->addStrongRef();
    else
        p->addWeakRef();
}

template <SharedIntrusiveRefCounted T>
template <class TT, bool IsAtomic>
requires std::convertible_to<TT*, T*>
SharedWeakUnion<T>::SharedWeakUnion(SharedIntrusive<TT, IsAtomic> const& rhs)
{
    auto p = rhs.unsafeGetRawPtr();
    if (p)
        p->addStrongRef();
    unsafeSetRawPtr(p, /*isStrong*/ true);
}

template <SharedIntrusiveRefCounted T>
SharedWeakUnion<T>::SharedWeakUnion(SharedWeakUnion&& rhs) : tp_{rhs.tp_}
{
    rhs.unsafeSetRawPtr(nullptr);
}

template <SharedIntrusiveRefCounted T>
template <class TT, bool IsAtomic>
requires std::convertible_to<TT*, T*>
SharedWeakUnion<T>::SharedWeakUnion(SharedIntrusive<TT, IsAtomic>&& rhs)
{
    auto p = rhs.unsafeGetRawPtr();
    if (p)
        unsafeSetRawPtr(p, /*isStrong*/ true);
    rhs.unsafeSetRawPtr(nullptr);
}

template <SharedIntrusiveRefCounted T>
SharedWeakUnion<T>&
SharedWeakUnion<T>::operator=(SharedWeakUnion const& rhs)
{
    if (this == &rhs)
        return *this;
    unsafeReleaseNoStore();

    if (auto p = rhs.unsafeGetRawPtr())
    {
        if (rhs.isStrong())
        {
            p->addStrongRef();
            unsafeSetRawPtr(p, /*isStrong*/ true);
        }
        else
        {
            p->addWeakRef();
            unsafeSetRawPtr(p, /*isStrong*/ false);
        }
    }
    else
    {
        unsafeSetRawPtr(nullptr);
    }
    return *this;
}

template <SharedIntrusiveRefCounted T>
template <class TT, bool IsAtomic>
requires std::convertible_to<TT*, T*> SharedWeakUnion<T>&
SharedWeakUnion<T>::operator=(SharedIntrusive<TT, IsAtomic> const& rhs)
{
    unsafeReleaseNoStore();
    auto p = rhs.unsafeGetRawPtr();
    if (p)
        p->addStrongRef();
    unsafeSetRawPtr(p, /*isStrong*/ true);
    return *this;
}

template <SharedIntrusiveRefCounted T>
template <class TT, bool IsAtomic>
requires std::convertible_to<TT*, T*> SharedWeakUnion<T>&
SharedWeakUnion<T>::operator=(SharedIntrusive<TT, IsAtomic>&& rhs)
{
    unsafeReleaseNoStore();
    unsafeSetRawPtr(rhs.unsafeGetRawPtr(), /*isStrong*/ true);
    rhs.unsafeSetRawPtr(nullptr);
    return *this;
}

template <SharedIntrusiveRefCounted T>
SharedWeakUnion<T>::~SharedWeakUnion()
{
    unsafeReleaseNoStore();
};

// Return a strong pointer if this is already a strong pointer (i.e. don't
// lock the weak pointer. Use the `lock` method if that's what's needed)
template <SharedIntrusiveRefCounted T>
SharedIntrusive<T, false>
SharedWeakUnion<T>::getStrong() const
{
    SharedIntrusive<T, false> result;
    auto p = unsafeGetRawPtr();
    if (p && isStrong())
    {
        result.template adopt<SharedIntrusiveAdoptIncrementStrongTag>(p);
    }
    return result;
}

template <SharedIntrusiveRefCounted T>
SharedWeakUnion<T>::operator bool() const noexcept
{
    return bool(get());
}

template <SharedIntrusiveRefCounted T>
void
SharedWeakUnion<T>::reset()
{
    unsafeReleaseNoStore();
    unsafeSetRawPtr(nullptr);
}

template <SharedIntrusiveRefCounted T>
T*
SharedWeakUnion<T>::get() const
{
    return isStrong() ? unsafeGetRawPtr() : nullptr;
}

template <SharedIntrusiveRefCounted T>
std::size_t
SharedWeakUnion<T>::use_count() const
{
    if (auto p = get())
        return p->use_count();
    return 0;
}

template <SharedIntrusiveRefCounted T>
bool
SharedWeakUnion<T>::expired() const
{
    auto p = unsafeGetRawPtr();
    return (!p || p->expired());
}

template <SharedIntrusiveRefCounted T>
SharedIntrusive<T, false>
SharedWeakUnion<T>::lock() const
{
    SharedIntrusive<T, false> result;
    auto p = unsafeGetRawPtr();
    if (!p)
        return result;

    if (isStrong())
    {
        result.template adopt<SharedIntrusiveAdoptIncrementStrongTag>(p);
        return result;
    }

    if (p->checkoutStrongRefFromWeak())
    {
        result.template adopt<SharedIntrusiveAdoptNoIncrementTag>(p);
        return result;
    }
    return result;
}

template <SharedIntrusiveRefCounted T>
bool
SharedWeakUnion<T>::isStrong() const
{
    return !(tp_ & tagMask);
}

template <SharedIntrusiveRefCounted T>
bool
SharedWeakUnion<T>::isWeak() const
{
    return tp_ & tagMask;
}

template <SharedIntrusiveRefCounted T>
bool
SharedWeakUnion<T>::convertToStrong()
{
    if (isStrong())
        return true;

    auto p = unsafeGetRawPtr();
    if (p && p->checkoutStrongRefFromWeak())
    {
        auto action = p->releaseWeakRef();
        (void)action;
        assert(action == ReleaseRefAction::noop);
        unsafeSetRawPtr(p, /*isStrong*/ true);
        return true;
    }
    return false;
}

template <SharedIntrusiveRefCounted T>
bool
SharedWeakUnion<T>::convertToWeak()
{
    if (isWeak())
        return true;

    auto p = unsafeGetRawPtr();
    if (!p)
        return false;

    // TODO: Could combine addWeak and releaseStrong into a single operation
    p->addWeakRef();
    using enum ReleaseRefAction;
    auto action = p->releaseStrongRef();
    switch (action)
    {
        case noop:
            break;
        case destroy:
            // We just added a weak ref. How could we destroy?
            assert(0);
            delete p;
            unsafeSetRawPtr(nullptr);
            return true;  // Should never happen
        case partialDestroy:
            // This is a weird case. We just converted the last strong
            // pointer to a weak pointer.
            p->partialDestructor();
            partialDestructorFinished(&p);
            // p is null and may no longer be used
            break;
    }
    unsafeSetRawPtr(p, /*isStrong*/ false);
    return true;
}

template <SharedIntrusiveRefCounted T>
T*
SharedWeakUnion<T>::unsafeGetRawPtr() const
{
    return reinterpret_cast<T*>(tp_ & ptrMask);
}

template <SharedIntrusiveRefCounted T>
void
SharedWeakUnion<T>::unsafeSetRawPtr(T* p, bool isStrong)
{
    tp_ = reinterpret_cast<std::uintptr_t>(p);
    if (tp_ && !isStrong)
        tp_ |= tagMask;
}

template <SharedIntrusiveRefCounted T>
void SharedWeakUnion<T>::unsafeSetRawPtr(std::nullptr_t)
{
    tp_ = 0;
}

template <SharedIntrusiveRefCounted T>
void
SharedWeakUnion<T>::unsafeReleaseNoStore()
{
    auto p = unsafeGetRawPtr();
    if (!p)
        return;

    using enum ReleaseRefAction;
    auto action = isStrong() ? p->releaseStrongRef() : p->releaseWeakRef();
    switch (action)
    {
        case noop:
            break;
        case destroy:
            delete p;
            break;
        case partialDestroy:
            p->partialDestructor();
            partialDestructorFinished(&p);
            // p is null and may no longer be used
            break;
    }
}

}  // namespace ripple
#endif
