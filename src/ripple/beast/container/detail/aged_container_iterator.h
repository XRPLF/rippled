//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_CONTAINER_DETAIL_AGED_CONTAINER_ITERATOR_H_INCLUDED
#define BEAST_CONTAINER_DETAIL_AGED_CONTAINER_ITERATOR_H_INCLUDED

#include <iterator>
#include <type_traits>

namespace beast {

template <bool, bool, class, class, class, class, class>
class aged_ordered_container;

namespace detail {

// Idea for Base template argument to prevent having to repeat
// the base class declaration comes from newbiz on ##c++/Freenode
//
// If Iterator is SCARY then this iterator will be as well.
template <
    bool is_const,
    class Iterator,
    class Base = std::iterator<
        typename std::iterator_traits<Iterator>::iterator_category,
        typename std::conditional<
            is_const,
            typename Iterator::value_type::stashed::value_type const,
            typename Iterator::value_type::stashed::value_type>::type,
        typename std::iterator_traits<Iterator>::difference_type>>
class aged_container_iterator : public Base
{
public:
    using time_point = typename Iterator::value_type::stashed::time_point;

    aged_container_iterator() = default;

    // Disable constructing a const_iterator from a non-const_iterator.
    // Converting between reverse and non-reverse iterators should be explicit.
    template <
        bool other_is_const,
        class OtherIterator,
        class OtherBase,
        class = typename std::enable_if<
            (other_is_const == false || is_const == true) &&
            std::is_same<Iterator, OtherIterator>::value == false>::type>
    explicit aged_container_iterator(
        aged_container_iterator<other_is_const, OtherIterator, OtherBase> const&
            other)
        : m_iter(other.m_iter)
    {
    }

    // Disable constructing a const_iterator from a non-const_iterator.
    template <
        bool other_is_const,
        class OtherBase,
        class = typename std::enable_if<
            other_is_const == false || is_const == true>::type>
    aged_container_iterator(
        aged_container_iterator<other_is_const, Iterator, OtherBase> const&
            other)
        : m_iter(other.m_iter)
    {
    }

    // Disable assigning a const_iterator to a non-const iterator
    template <bool other_is_const, class OtherIterator, class OtherBase>
    auto
    operator=(
        aged_container_iterator<other_is_const, OtherIterator, OtherBase> const&
            other) ->
        typename std::enable_if<
            other_is_const == false || is_const == true,
            aged_container_iterator&>::type
    {
        m_iter = other.m_iter;
        return *this;
    }

    template <bool other_is_const, class OtherIterator, class OtherBase>
    bool
    operator==(
        aged_container_iterator<other_is_const, OtherIterator, OtherBase> const&
            other) const
    {
        return m_iter == other.m_iter;
    }

    template <bool other_is_const, class OtherIterator, class OtherBase>
    bool
    operator!=(
        aged_container_iterator<other_is_const, OtherIterator, OtherBase> const&
            other) const
    {
        return m_iter != other.m_iter;
    }

    aged_container_iterator&
    operator++()
    {
        ++m_iter;
        return *this;
    }

    aged_container_iterator
    operator++(int)
    {
        aged_container_iterator const prev(*this);
        ++m_iter;
        return prev;
    }

    aged_container_iterator&
    operator--()
    {
        --m_iter;
        return *this;
    }

    aged_container_iterator
    operator--(int)
    {
        aged_container_iterator const prev(*this);
        --m_iter;
        return prev;
    }

    typename Base::reference
    operator*() const
    {
        return m_iter->value;
    }

    typename Base::pointer
    operator->() const
    {
        return &m_iter->value;
    }

    time_point const&
    when() const
    {
        return m_iter->when;
    }

private:
    template <bool, bool, class, class, class, class, class>
    friend class aged_ordered_container;

    template <bool, bool, class, class, class, class, class, class>
    friend class aged_unordered_container;

    template <bool, class, class>
    friend class aged_container_iterator;

    template <class OtherIterator>
    aged_container_iterator(OtherIterator const& iter) : m_iter(iter)
    {
    }

    Iterator const&
    iterator() const
    {
        return m_iter;
    }

    Iterator m_iter;
};

}  // namespace detail

}  // namespace beast

#endif
