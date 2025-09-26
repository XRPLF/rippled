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

#ifndef XRPL_BASICS_SHAREDWEAKCACHEPOINTER_IPP_INCLUDED
#define XRPL_BASICS_SHAREDWEAKCACHEPOINTER_IPP_INCLUDED

#include <xrpl/basics/SharedWeakCachePointer.h>

namespace ripple {
template <class T>
SharedWeakCachePointer<T>::SharedWeakCachePointer(
    SharedWeakCachePointer const& rhs) = default;

template <class T>
template <class TT>
    requires std::convertible_to<TT*, T*>
SharedWeakCachePointer<T>::SharedWeakCachePointer(
    std::shared_ptr<TT> const& rhs)
    : combo_{rhs}
{
}

template <class T>
SharedWeakCachePointer<T>::SharedWeakCachePointer(
    SharedWeakCachePointer&& rhs) = default;

template <class T>
template <class TT>
    requires std::convertible_to<TT*, T*>
SharedWeakCachePointer<T>::SharedWeakCachePointer(std::shared_ptr<TT>&& rhs)
    : combo_{std::move(rhs)}
{
}

template <class T>
SharedWeakCachePointer<T>&
SharedWeakCachePointer<T>::operator=(SharedWeakCachePointer const& rhs) =
    default;

template <class T>
template <class TT>
    requires std::convertible_to<TT*, T*>
SharedWeakCachePointer<T>&
SharedWeakCachePointer<T>::operator=(std::shared_ptr<TT> const& rhs)
{
    combo_ = rhs;
    return *this;
}

template <class T>
template <class TT>
    requires std::convertible_to<TT*, T*>
SharedWeakCachePointer<T>&
SharedWeakCachePointer<T>::operator=(std::shared_ptr<TT>&& rhs)
{
    combo_ = std::move(rhs);
    return *this;
}

template <class T>
SharedWeakCachePointer<T>::~SharedWeakCachePointer() = default;

// Return a strong pointer if this is already a strong pointer (i.e. don't
// lock the weak pointer. Use the `lock` method if that's what's needed)
template <class T>
std::shared_ptr<T> const&
SharedWeakCachePointer<T>::getStrong() const
{
    static std::shared_ptr<T> const empty;
    if (auto p = std::get_if<std::shared_ptr<T>>(&combo_))
        return *p;
    return empty;
}

template <class T>
SharedWeakCachePointer<T>::operator bool() const noexcept
{
    return !!std::get_if<std::shared_ptr<T>>(&combo_);
}

template <class T>
void
SharedWeakCachePointer<T>::reset()
{
    combo_ = std::shared_ptr<T>{};
}

template <class T>
T*
SharedWeakCachePointer<T>::get() const
{
    return std::get_if<std::shared_ptr<T>>(&combo_).get();
}

template <class T>
std::size_t
SharedWeakCachePointer<T>::use_count() const
{
    if (auto p = std::get_if<std::shared_ptr<T>>(&combo_))
        return p->use_count();
    return 0;
}

template <class T>
bool
SharedWeakCachePointer<T>::expired() const
{
    if (auto p = std::get_if<std::weak_ptr<T>>(&combo_))
        return p->expired();
    return !std::get_if<std::shared_ptr<T>>(&combo_);
}

template <class T>
std::shared_ptr<T>
SharedWeakCachePointer<T>::lock() const
{
    if (auto p = std::get_if<std::shared_ptr<T>>(&combo_))
        return *p;

    if (auto p = std::get_if<std::weak_ptr<T>>(&combo_))
        return p->lock();

    return {};
}

template <class T>
bool
SharedWeakCachePointer<T>::isStrong() const
{
    if (auto p = std::get_if<std::shared_ptr<T>>(&combo_))
        return !!p->get();
    return false;
}

template <class T>
bool
SharedWeakCachePointer<T>::isWeak() const
{
    return !isStrong();
}

template <class T>
bool
SharedWeakCachePointer<T>::convertToStrong()
{
    if (isStrong())
        return true;

    if (auto p = std::get_if<std::weak_ptr<T>>(&combo_))
    {
        if (auto s = p->lock())
        {
            combo_ = std::move(s);
            return true;
        }
    }
    return false;
}

template <class T>
bool
SharedWeakCachePointer<T>::convertToWeak()
{
    if (isWeak())
        return true;

    if (auto p = std::get_if<std::shared_ptr<T>>(&combo_))
    {
        combo_ = std::weak_ptr<T>(*p);
        return true;
    }

    return false;
}
}  // namespace ripple
#endif
