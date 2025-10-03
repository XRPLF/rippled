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

#include <xrpl/basics/IntrusivePointer.h>
#include <xrpl/basics/IntrusiveRefCounts.h>

#include <utility>

namespace ripple {

template <class T>
template <CAdoptTag TAdoptTag>
SharedIntrusive<T>::SharedIntrusive(T* p, TAdoptTag) noexcept : ptr_{p}
{
    if constexpr (std::is_same_v<
                      TAdoptTag,
                      SharedIntrusiveAdoptIncrementStrongTag>)
    {
        if (p)
            p->addStrongRef();
    }
}

template <class T>
SharedIntrusive<T>::SharedIntrusive(SharedIntrusive const& rhs)
    : ptr_{[&] {
        auto p = rhs.unsafeGetRawPtr();
        if (p)
            p->addStrongRef();
        return p;
    }()}
{
}

template <class T>
template <class TT>
    requires std::convertible_to<TT*, T*>
SharedIntrusive<T>::SharedIntrusive(SharedIntrusive<TT> const& rhs)
    : ptr_{[&] {
        auto p = rhs.unsafeGetRawPtr();
        if (p)
            p->addStrongRef();
        return p;
    }()}
{
}

template <class T>
SharedIntrusive<T>::SharedIntrusive(SharedIntrusive&& rhs)
    : ptr_{rhs.unsafeExchange(nullptr)}
{
}

template <class T>
template <class TT>
    requires std::convertible_to<TT*, T*>
SharedIntrusive<T>::SharedIntrusive(SharedIntrusive<TT>&& rhs)
    : ptr_{rhs.unsafeExchange(nullptr)}
{
}
template <class T>
SharedIntrusive<T>&
SharedIntrusive<T>::operator=(SharedIntrusive const& rhs)
{
    if (this == &rhs)
        return *this;
    auto p = rhs.unsafeGetRawPtr();
    if (p)
        p->addStrongRef();
    unsafeReleaseAndStore(p);
    return *this;
}

template <class T>
template <class TT>
// clang-format off
requires std::convertible_to<TT*, T*>
// clang-format on
SharedIntrusive<T>&
SharedIntrusive<T>::operator=(SharedIntrusive<TT> const& rhs)
{
    if constexpr (std::is_same_v<T, TT>)
    {
        // This case should never be hit. The operator above will run instead.
        // (The normal operator= is needed or it will be marked `deleted`)
        if (this == &rhs)
            return *this;
    }
    auto p = rhs.unsafeGetRawPtr();
    if (p)
        p->addStrongRef();
    unsafeReleaseAndStore(p);
    return *this;
}

template <class T>
SharedIntrusive<T>&
SharedIntrusive<T>::operator=(SharedIntrusive&& rhs)
{
    if (this == &rhs)
        return *this;

    unsafeReleaseAndStore(rhs.unsafeExchange(nullptr));
    return *this;
}

template <class T>
template <class TT>
// clang-format off
requires std::convertible_to<TT*, T*>
// clang-format on
SharedIntrusive<T>&
SharedIntrusive<T>::operator=(SharedIntrusive<TT>&& rhs)
{
    static_assert(
        !std::is_same_v<T, TT>,
        "This overload should not be instantiated for T == TT");

    unsafeReleaseAndStore(rhs.unsafeExchange(nullptr));
    return *this;
}

template <class T>
bool
SharedIntrusive<T>::operator!=(std::nullptr_t) const
{
    return this->get() != nullptr;
}

template <class T>
bool
SharedIntrusive<T>::operator==(std::nullptr_t) const
{
    return this->get() == nullptr;
}

template <class T>
template <CAdoptTag TAdoptTag>
void
SharedIntrusive<T>::adopt(T* p)
{
    if constexpr (std::is_same_v<
                      TAdoptTag,
                      SharedIntrusiveAdoptIncrementStrongTag>)
    {
        if (p)
            p->addStrongRef();
    }
    unsafeReleaseAndStore(p);
}

template <class T>
SharedIntrusive<T>::~SharedIntrusive()
{
    unsafeReleaseAndStore(nullptr);
};

template <class T>
template <class TT>
SharedIntrusive<T>::SharedIntrusive(
    StaticCastTagSharedIntrusive,
    SharedIntrusive<TT> const& rhs)
    : ptr_{[&] {
        auto p = static_cast<T*>(rhs.unsafeGetRawPtr());
        if (p)
            p->addStrongRef();
        return p;
    }()}
{
}

template <class T>
template <class TT>
SharedIntrusive<T>::SharedIntrusive(
    StaticCastTagSharedIntrusive,
    SharedIntrusive<TT>&& rhs)
    : ptr_{static_cast<T*>(rhs.unsafeExchange(nullptr))}
{
}

template <class T>
template <class TT>
SharedIntrusive<T>::SharedIntrusive(
    DynamicCastTagSharedIntrusive,
    SharedIntrusive<TT> const& rhs)
    : ptr_{[&] {
        auto p = dynamic_cast<T*>(rhs.unsafeGetRawPtr());
        if (p)
            p->addStrongRef();
        return p;
    }()}
{
}

template <class T>
template <class TT>
SharedIntrusive<T>::SharedIntrusive(
    DynamicCastTagSharedIntrusive,
    SharedIntrusive<TT>&& rhs)
{
    // This can be simplified without the `exchange`, but the `exchange` is kept
    // in anticipation of supporting atomic operations.
    auto toSet = rhs.unsafeExchange(nullptr);
    if (toSet)
    {
        ptr_ = dynamic_cast<T*>(toSet);
        if (!ptr_)
            // need to set the pointer back or will leak
            rhs.unsafeExchange(toSet);
    }
}

template <class T>
T&
SharedIntrusive<T>::operator*() const noexcept
{
    return *unsafeGetRawPtr();
}

template <class T>
T*
SharedIntrusive<T>::operator->() const noexcept
{
    return unsafeGetRawPtr();
}

template <class T>
SharedIntrusive<T>::operator bool() const noexcept
{
    return bool(unsafeGetRawPtr());
}

template <class T>
void
SharedIntrusive<T>::reset()
{
    unsafeReleaseAndStore(nullptr);
}

template <class T>
T*
SharedIntrusive<T>::get() const
{
    return unsafeGetRawPtr();
}

template <class T>
std::size_t
SharedIntrusive<T>::use_count() const
{
    if (auto p = unsafeGetRawPtr())
        return p->use_count();
    return 0;
}

template <class T>
T*
SharedIntrusive<T>::unsafeGetRawPtr() const
{
    return ptr_;
}

template <class T>
void
SharedIntrusive<T>::unsafeSetRawPtr(T* p)
{
    ptr_ = p;
}

template <class T>
T*
SharedIntrusive<T>::unsafeExchange(T* p)
{
    return std::exchange(ptr_, p);
}

template <class T>
void
SharedIntrusive<T>::unsafeReleaseAndStore(T* next)
{
    auto prev = unsafeExchange(next);
    if (!prev)
        return;

    using enum ReleaseStrongRefAction;
    auto action = prev->releaseStrongRef();
    switch (action)
    {
        case noop:
            break;
        case destroy:
            delete prev;
            break;
        case partialDestroy:
            prev->partialDestructor();
            partialDestructorFinished(&prev);
            // prev is null and may no longer be used
            break;
    }
}

//------------------------------------------------------------------------------

template <class T>
WeakIntrusive<T>::WeakIntrusive(WeakIntrusive const& rhs) : ptr_{rhs.ptr_}
{
    if (ptr_)
        ptr_->addWeakRef();
}

template <class T>
WeakIntrusive<T>::WeakIntrusive(WeakIntrusive&& rhs) : ptr_{rhs.ptr_}
{
    rhs.ptr_ = nullptr;
}

template <class T>
WeakIntrusive<T>::WeakIntrusive(SharedIntrusive<T> const& rhs)
    : ptr_{rhs.unsafeGetRawPtr()}
{
    if (ptr_)
        ptr_->addWeakRef();
}

template <class T>
template <class TT>
// clang-format off
requires std::convertible_to<TT*, T*>
// clang-format on
WeakIntrusive<T>&
WeakIntrusive<T>::operator=(SharedIntrusive<TT> const& rhs)
{
    unsafeReleaseNoStore();
    auto p = rhs.unsafeGetRawPtr();
    if (p)
        p->addWeakRef();
    return *this;
}

template <class T>
void
WeakIntrusive<T>::adopt(T* ptr)
{
    unsafeReleaseNoStore();
    if (ptr)
        ptr->addWeakRef();
    ptr_ = ptr;
}

template <class T>
WeakIntrusive<T>::~WeakIntrusive()
{
    unsafeReleaseNoStore();
}

template <class T>
SharedIntrusive<T>
WeakIntrusive<T>::lock() const
{
    if (ptr_ && ptr_->checkoutStrongRefFromWeak())
    {
        return SharedIntrusive<T>{ptr_, SharedIntrusiveAdoptNoIncrementTag{}};
    }
    return {};
}

template <class T>
bool
WeakIntrusive<T>::expired() const
{
    return (!ptr_ || ptr_->expired());
}

template <class T>
void
WeakIntrusive<T>::reset()
{
    unsafeReleaseNoStore();
    ptr_ = nullptr;
}

template <class T>
void
WeakIntrusive<T>::unsafeReleaseNoStore()
{
    if (!ptr_)
        return;

    using enum ReleaseWeakRefAction;
    auto action = ptr_->releaseWeakRef();
    switch (action)
    {
        case noop:
            break;
        case destroy:
            delete ptr_;
            break;
    }
}

//------------------------------------------------------------------------------

template <class T>
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

template <class T>
template <class TT>
    requires std::convertible_to<TT*, T*>
SharedWeakUnion<T>::SharedWeakUnion(SharedIntrusive<TT> const& rhs)
{
    auto p = rhs.unsafeGetRawPtr();
    if (p)
        p->addStrongRef();
    unsafeSetRawPtr(p, RefStrength::strong);
}

template <class T>
SharedWeakUnion<T>::SharedWeakUnion(SharedWeakUnion&& rhs) : tp_{rhs.tp_}
{
    rhs.unsafeSetRawPtr(nullptr);
}

template <class T>
template <class TT>
    requires std::convertible_to<TT*, T*>
SharedWeakUnion<T>::SharedWeakUnion(SharedIntrusive<TT>&& rhs)
{
    auto p = rhs.unsafeGetRawPtr();
    if (p)
        unsafeSetRawPtr(p, RefStrength::strong);
    rhs.unsafeSetRawPtr(nullptr);
}

template <class T>
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
            unsafeSetRawPtr(p, RefStrength::strong);
        }
        else
        {
            p->addWeakRef();
            unsafeSetRawPtr(p, RefStrength::weak);
        }
    }
    else
    {
        unsafeSetRawPtr(nullptr);
    }
    return *this;
}

template <class T>
template <class TT>
// clang-format off
requires std::convertible_to<TT*, T*>
// clang-format on
SharedWeakUnion<T>&
SharedWeakUnion<T>::operator=(SharedIntrusive<TT> const& rhs)
{
    unsafeReleaseNoStore();
    auto p = rhs.unsafeGetRawPtr();
    if (p)
        p->addStrongRef();
    unsafeSetRawPtr(p, RefStrength::strong);
    return *this;
}

template <class T>
template <class TT>
// clang-format off
requires std::convertible_to<TT*, T*>
// clang-format on
SharedWeakUnion<T>&
SharedWeakUnion<T>::operator=(SharedIntrusive<TT>&& rhs)
{
    unsafeReleaseNoStore();
    unsafeSetRawPtr(rhs.unsafeGetRawPtr(), RefStrength::strong);
    rhs.unsafeSetRawPtr(nullptr);
    return *this;
}

template <class T>
SharedWeakUnion<T>::~SharedWeakUnion()
{
    unsafeReleaseNoStore();
};

// Return a strong pointer if this is already a strong pointer (i.e. don't
// lock the weak pointer. Use the `lock` method if that's what's needed)
template <class T>
SharedIntrusive<T>
SharedWeakUnion<T>::getStrong() const
{
    SharedIntrusive<T> result;
    auto p = unsafeGetRawPtr();
    if (p && isStrong())
    {
        result.template adopt<SharedIntrusiveAdoptIncrementStrongTag>(p);
    }
    return result;
}

template <class T>
SharedWeakUnion<T>::operator bool() const noexcept
{
    return bool(get());
}

template <class T>
void
SharedWeakUnion<T>::reset()
{
    unsafeReleaseNoStore();
    unsafeSetRawPtr(nullptr);
}

template <class T>
T*
SharedWeakUnion<T>::get() const
{
    return isStrong() ? unsafeGetRawPtr() : nullptr;
}

template <class T>
std::size_t
SharedWeakUnion<T>::use_count() const
{
    if (auto p = get())
        return p->use_count();
    return 0;
}

template <class T>
bool
SharedWeakUnion<T>::expired() const
{
    auto p = unsafeGetRawPtr();
    return (!p || p->expired());
}

template <class T>
SharedIntrusive<T>
SharedWeakUnion<T>::lock() const
{
    SharedIntrusive<T> result;
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

template <class T>
bool
SharedWeakUnion<T>::isStrong() const
{
    return !(tp_ & tagMask);
}

template <class T>
bool
SharedWeakUnion<T>::isWeak() const
{
    return tp_ & tagMask;
}

template <class T>
bool
SharedWeakUnion<T>::convertToStrong()
{
    if (isStrong())
        return true;

    auto p = unsafeGetRawPtr();
    if (p && p->checkoutStrongRefFromWeak())
    {
        [[maybe_unused]] auto action = p->releaseWeakRef();
        XRPL_ASSERT(
            (action == ReleaseWeakRefAction::noop),
            "ripple::SharedWeakUnion::convertToStrong : "
            "action is noop");
        unsafeSetRawPtr(p, RefStrength::strong);
        return true;
    }
    return false;
}

template <class T>
bool
SharedWeakUnion<T>::convertToWeak()
{
    if (isWeak())
        return true;

    auto p = unsafeGetRawPtr();
    if (!p)
        return false;

    using enum ReleaseStrongRefAction;
    auto action = p->addWeakReleaseStrongRef();
    switch (action)
    {
        case noop:
            break;
        case destroy:
            // We just added a weak ref. How could we destroy?
            // LCOV_EXCL_START
            UNREACHABLE(
                "ripple::SharedWeakUnion::convertToWeak : destroying freshly "
                "added ref");
            delete p;
            unsafeSetRawPtr(nullptr);
            return true;  // Should never happen
            // LCOV_EXCL_STOP
        case partialDestroy:
            // This is a weird case. We just converted the last strong
            // pointer to a weak pointer.
            p->partialDestructor();
            partialDestructorFinished(&p);
            // p is null and may no longer be used
            break;
    }
    unsafeSetRawPtr(p, RefStrength::weak);
    return true;
}

template <class T>
T*
SharedWeakUnion<T>::unsafeGetRawPtr() const
{
    return reinterpret_cast<T*>(tp_ & ptrMask);
}

template <class T>
void
SharedWeakUnion<T>::unsafeSetRawPtr(T* p, RefStrength rs)
{
    tp_ = reinterpret_cast<std::uintptr_t>(p);
    if (tp_ && rs == RefStrength::weak)
        tp_ |= tagMask;
}

template <class T>
void
SharedWeakUnion<T>::unsafeSetRawPtr(std::nullptr_t)
{
    tp_ = 0;
}

template <class T>
void
SharedWeakUnion<T>::unsafeReleaseNoStore()
{
    auto p = unsafeGetRawPtr();
    if (!p)
        return;

    if (isStrong())
    {
        using enum ReleaseStrongRefAction;
        auto strongAction = p->releaseStrongRef();
        switch (strongAction)
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
    else
    {
        using enum ReleaseWeakRefAction;
        auto weakAction = p->releaseWeakRef();
        switch (weakAction)
        {
            case noop:
                break;
            case destroy:
                delete p;
                break;
        }
    }
}

}  // namespace ripple
#endif
