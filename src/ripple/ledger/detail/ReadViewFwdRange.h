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

#ifndef RIPPLE_LEDGER_READVIEWFWDRANGE_H_INCLUDED
#define RIPPLE_LEDGER_READVIEWFWDRANGE_H_INCLUDED

#include <boost/optional.hpp>
#include <cstddef>
#include <iterator>
#include <memory>

namespace ripple {

class ReadView;

namespace detail {

// A type-erased ForwardIterator
//
template <class ValueType>
class ReadViewFwdIter
{
public:
    using base_type = ReadViewFwdIter;

    using value_type = ValueType;

    ReadViewFwdIter() = default;
    ReadViewFwdIter(ReadViewFwdIter const&) = default;
    ReadViewFwdIter&
    operator=(ReadViewFwdIter const&) = default;

    virtual ~ReadViewFwdIter() = default;

    virtual std::unique_ptr<ReadViewFwdIter>
    copy() const = 0;

    virtual bool
    equal(ReadViewFwdIter const& impl) const = 0;

    virtual void
    increment() = 0;

    virtual value_type
    dereference() const = 0;
};

// A range using type-erased ForwardIterator
//
template <class ValueType>
class ReadViewFwdRange
{
public:
    using iter_base = ReadViewFwdIter<ValueType>;

    static_assert(
        std::is_nothrow_move_constructible<ValueType>{},
        "ReadViewFwdRange move and move assign constructors should be "
        "noexcept");

    class iterator
    {
    public:
        using value_type = ValueType;

        using pointer = value_type const*;

        using reference = value_type const&;

        using difference_type = std::ptrdiff_t;

        using iterator_category = std::forward_iterator_tag;

        iterator() = default;

        iterator(iterator const& other);
        iterator(iterator&& other) noexcept;

        // Used by the implementation
        explicit iterator(
            ReadView const* view,
            std::unique_ptr<iter_base> impl);

        iterator&
        operator=(iterator const& other);

        iterator&
        operator=(iterator&& other) noexcept;

        bool
        operator==(iterator const& other) const;

        bool
        operator!=(iterator const& other) const;

        // Can throw
        reference
        operator*() const;

        // Can throw
        pointer
        operator->() const;

        iterator&
        operator++();

        iterator
        operator++(int);

    private:
        ReadView const* view_ = nullptr;
        std::unique_ptr<iter_base> impl_;
        boost::optional<value_type> mutable cache_;
    };

    static_assert(std::is_nothrow_move_constructible<iterator>{}, "");
    static_assert(std::is_nothrow_move_assignable<iterator>{}, "");

    using const_iterator = iterator;

    using value_type = ValueType;

    ReadViewFwdRange() = delete;
    ReadViewFwdRange(ReadViewFwdRange const&) = default;
    ReadViewFwdRange&
    operator=(ReadViewFwdRange const&) = default;

    explicit ReadViewFwdRange(ReadView const& view) : view_(&view)
    {
    }

protected:
    ReadView const* view_;
};

}  // namespace detail
}  // namespace ripple

#endif
