//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_LEDGER_READVIEWFWDRANGEINL_H_INCLUDED
#define RIPPLE_LEDGER_READVIEWFWDRANGEINL_H_INCLUDED

namespace ripple {
namespace detail {

template <class ValueType>
ReadViewFwdRange<ValueType>::iterator::iterator(iterator const& other)
    : view_(other.view_)
    , impl_(other.impl_ ? other.impl_->copy() : nullptr)
    , cache_(other.cache_)
{
}

template <class ValueType>
ReadViewFwdRange<ValueType>::iterator::iterator(iterator&& other) noexcept
    : view_(other.view_)
    , impl_(std::move(other.impl_))
    , cache_(std::move(other.cache_))
{
}

template <class ValueType>
ReadViewFwdRange<ValueType>::iterator::iterator(
    ReadView const* view,
    std::unique_ptr<iter_base> impl)
    : view_(view), impl_(std::move(impl))
{
}

template <class ValueType>
auto
ReadViewFwdRange<ValueType>::iterator::operator=(iterator const& other)
    -> iterator&
{
    if (this != &other)
    {
        view_ = other.view_;
        impl_ = other.impl_ ? other.impl_->copy() : nullptr;
        cache_ = other.cache_;
    }
    return *this;
}

template <class ValueType>
auto
ReadViewFwdRange<ValueType>::iterator::operator=(iterator&& other) noexcept
    -> iterator&
{
    if (this != &other)
    {
        view_ = other.view_;
        impl_ = std::move(other.impl_);
        cache_ = std::move(other.cache_);
    }

    return *this;
}

template <class ValueType>
bool
ReadViewFwdRange<ValueType>::iterator::operator==(iterator const& other) const
{
    ASSERT(
        view_ == other.view_,
        "ripple::detail::ReadViewFwdRange::iterator::operator==(iterator) "
        "const : input view match");

    if (impl_ != nullptr && other.impl_ != nullptr)
        return impl_->equal(*other.impl_);

    return impl_ == other.impl_;
}

template <class ValueType>
bool
ReadViewFwdRange<ValueType>::iterator::operator!=(iterator const& other) const
{
    return !(*this == other);
}

template <class ValueType>
auto
ReadViewFwdRange<ValueType>::iterator::operator*() const -> reference
{
    if (!cache_)
        cache_ = impl_->dereference();
    return *cache_;
}

template <class ValueType>
auto
ReadViewFwdRange<ValueType>::iterator::operator->() const -> pointer
{
    return &**this;
}

template <class ValueType>
auto
ReadViewFwdRange<ValueType>::iterator::operator++() -> iterator&
{
    impl_->increment();
    cache_.reset();
    return *this;
}

template <class ValueType>
auto
ReadViewFwdRange<ValueType>::iterator::operator++(int) -> iterator
{
    iterator prev(view_, impl_->copy());
    prev.cache_ = std::move(cache_);
    ++(*this);
    return prev;
}

}  // namespace detail
}  // namespace ripple

#endif
