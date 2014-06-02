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

#ifndef BEAST_CONTAINER_BUFFER_VIEW_H_INCLUDED
#define BEAST_CONTAINER_BUFFER_VIEW_H_INCLUDED

#include <beast/Config.h>

#include <array>
#include <beast/cxx14/algorithm.h> // <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#include <beast/cxx14/type_traits.h> // <type_traits>

namespace beast {

namespace detail {

template <class T, class U,
    bool = std::is_const <std::remove_reference_t <T>>::value>
struct apply_const
{
    typedef U type;
};

template <class T, class U>
struct apply_const <T, U, true>
{
    typedef const U type;
};

// is_contiguous is true if C is a contiguous container
template <class C>
struct is_contiguous
    : public std::false_type
{
};

template <class C>
struct is_contiguous <C const>
    : public is_contiguous <C>
{
};

template <class T, class Alloc>
struct is_contiguous <std::vector <T, Alloc>>
    : public std::true_type
{
};

template <class CharT, class Traits, class Alloc>
struct is_contiguous <std::basic_string<
    CharT, Traits, Alloc>>
    : public std::true_type
{
};

template <class T, std::size_t N>
struct is_contiguous <std::array<T, N>>
    : public std::true_type
{
};

// True if T is const or U is not const
template <class T, class U>
struct buffer_view_const_compatible : std::integral_constant <bool,
    std::is_const<T>::value || ! std::is_const<U>::value
>
{
};

// True if T and U are the same or differ only in const, or
// if T and U are equally sized integral types.
template <class T, class U>
struct buffer_view_ptr_compatible : std::integral_constant <bool,
    (std::is_same <std::remove_const <T>, std::remove_const <U>>::value) ||
        (std::is_integral <T>::value && std::is_integral <U>::value &&
            sizeof (U) == sizeof (T))
>
{
};

// Determine if buffer_view <T, ..> is constructible from U*
template <class T, class U>
struct buffer_view_convertible : std::integral_constant <bool,
    buffer_view_const_compatible <T, U>::value &&
        buffer_view_ptr_compatible <T, U>::value
>
{
};

// True if C is a container that can be used to construct a buffer_view<T>
template <class T, class C>
struct buffer_view_container_compatible : std::integral_constant <bool,
    is_contiguous <C>::value && buffer_view_convertible <T,
        typename apply_const <C, typename C::value_type>::type>::value
>
{
};

} // detail

struct buffer_view_default_tag
{
};

//------------------------------------------------------------------------------

/** A view into a range of contiguous container elements.

    The size of the view is determined at the time of construction.
    This tries to emulate the interface of std::vector as closely as possible,
    with the constraint that the size of the container cannot be changed.

    @tparam T The underlying element type. If T is const, member functions
              which can modify elements are removed from the interface.

    @tparam Tag A type used to prevent two views with the same T from being
                comparable or assignable.
*/
template <
    class T,
    class Tag = buffer_view_default_tag
>
class buffer_view
{
private:
    T* m_base;
    std::size_t m_size;

    static_assert (std::is_same <T, std::remove_reference_t <T>>::value,
        "T may not be a reference type");

    static_assert (! std::is_same <T, void>::value,
        "T may not be void");

    static_assert (std::is_same <std::add_const_t <T>,
        std::remove_reference_t <T> const>::value,
            "Expected std::add_const to produce T const");

    template <class Iter>
    void
    assign (Iter first, Iter last) noexcept
    {
        typedef typename std::iterator_traits <Iter>::value_type U;

        static_assert (detail::buffer_view_const_compatible <T, U>::value,
            "Cannot convert from 'U const' to 'T', "
                "conversion loses const qualifiers");

        static_assert (detail::buffer_view_ptr_compatible <T, U>::value,
            "Cannot convert from 'U*' to 'T*, "
                "types are incompatible");

        if (first == last)
        {
            m_base = nullptr;
            m_size = 0;
        }
        else
        {
        #if 0
            // fails on gcc
            m_base = reinterpret_cast <T*> (
                std::addressof (*first));
        #else
            m_base = reinterpret_cast <T*> (&*first);
        #endif
            m_size = std::distance (first, last);
        }
    }

public:
    typedef T value_type;
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;
    typedef T& reference;
    typedef T const& const_reference;
    typedef T* pointer;
    typedef T const* const_pointer;
    typedef T* iterator;
    typedef T const* const_iterator;
    typedef std::reverse_iterator <iterator> reverse_iterator;
    typedef std::reverse_iterator <const_iterator> const_reverse_iterator;

    // default construct
    buffer_view () noexcept
        : m_base (nullptr)
        , m_size (0)
    {
    }

    // copy construct
    template <class U,
        class = std::enable_if_t <
            detail::buffer_view_convertible <T, U>::value>
    >
    buffer_view (buffer_view <U, Tag> v) noexcept
    {
        assign (v.begin(), v.end());
    }

    // construct from container
    template <class C,
        class = std::enable_if_t <
            detail::buffer_view_container_compatible <T, C>::value
        >
    >
    buffer_view (C& c) noexcept
    {
        assign (c.begin(), c.end());
    }

    // construct from pointer range
    template <class U,
        class = std::enable_if_t <
            detail::buffer_view_convertible <T, U>::value>
    >
    buffer_view (U* first, U* last) noexcept
    {
        assign (first, last);
    }

    // construct from base and size
    template <class U,
        class = std::enable_if_t <
            detail::buffer_view_convertible <T, U>::value>
    >
    buffer_view (U* u, std::size_t n) noexcept
        : m_base (u)
        , m_size (n)
    {
    }

    // assign from container
    template <class C,
        class = std::enable_if_t <
            detail::buffer_view_container_compatible <T, C>::value
        >
    >
    buffer_view&
    operator= (C& c) noexcept
    {
        assign (c.begin(), c.end());
        return *this;
    }

    //
    // Element access
    //

    reference
    at (size_type pos)
    {
        if (! (pos < size()))
            throw std::out_of_range ("bad array index");
        return m_base [pos];
    }

    const_reference
    at (size_type pos) const
    {
        if (! (pos < size()))
            throw std::out_of_range ("bad array index");
        return m_base [pos];
    }

    reference
    operator[] (size_type pos) noexcept
    {
        return m_base [pos];
    }

    const_reference
    operator[] (size_type pos) const noexcept
    {
        return m_base [pos];
    }

    reference
    back() noexcept
    {
        return m_base [m_size - 1];
    }

    const_reference
    back() const noexcept
    {
        return m_base [m_size - 1];
    }

    reference
    front() noexcept
    {
        return *m_base;
    }

    const_reference
    front() const noexcept
    {
        return *m_base;
    }

    pointer
    data() noexcept
    {
        return m_base;
    }

    const_pointer
    data() const noexcept
    {
        return m_base;
    }

    //
    // Iterators
    //

    iterator
    begin() noexcept
    {
        return m_base;
    }

    const_iterator
    begin() const noexcept
    {
        return m_base;
    }

    const_iterator
    cbegin() const noexcept
    {
        return m_base;
    }

    iterator
    end() noexcept
    {
        return m_base + m_size;
    }

    const_iterator
    end() const noexcept
    {
        return m_base + m_size;
    }

    const_iterator
    cend() const noexcept
    {
        return m_base + m_size;
    }

    reverse_iterator
    rbegin() noexcept
    {
        return reverse_iterator (end());
    }

    const_reverse_iterator
    rbegin() const noexcept
    {
        return const_reverse_iterator (cend());
    }

    const_reverse_iterator
    crbegin() const noexcept
    {
        return const_reverse_iterator (cend());
    }

    reverse_iterator
    rend() noexcept
    {
        return reverse_iterator (begin());
    }

    const_reverse_iterator
    rend() const noexcept
    {
        return const_reverse_iterator (cbegin());
    }

    const_reverse_iterator
    crend() const noexcept
    {
        return const_reverse_iterator (cbegin());
    }

    //
    // Capacity
    //

    bool
    empty() const noexcept
    {
        return m_size == 0;
    }

    size_type
    size() const noexcept
    {
        return m_size;
    }

    size_type
    max_size() const noexcept
    {
        return size();
    }

    size_type
    capacity() const noexcept
    {
        return size();
    }

    //
    // Modifiers
    //

    template <class U, class K>
    friend void swap (buffer_view <U, K>& lhs,
        buffer_view <U, K>& rhs) noexcept;
};

//------------------------------------------------------------------------------

template <class T, class Tag>
inline
bool
operator== (buffer_view <T, Tag> lhs, buffer_view <T, Tag> rhs)
{
    return std::equal (
        lhs.cbegin(), lhs.cend(), rhs.cbegin(), rhs.cend());
}

template <class T, class Tag>
inline
bool
operator!= (buffer_view <T, Tag> lhs, buffer_view <T, Tag> rhs)
{
    return ! (lhs == rhs);
}

template <class T, class Tag>
inline
bool
operator< (buffer_view <T, Tag> lhs, buffer_view <T, Tag> rhs)
{
    return std::lexicographical_compare (
        lhs.cbegin(), lhs.cend(), rhs.cbegin(), rhs.cend());
}

template <class T, class Tag>
inline
bool
operator>= (buffer_view <T, Tag> lhs, buffer_view <T, Tag> rhs)
{
    return ! (lhs < rhs);
}

template <class T, class Tag>
inline
bool
operator> (buffer_view <T, Tag> lhs, buffer_view <T, Tag> rhs)
{
    return rhs < lhs;
}

template <class T, class Tag>
inline
bool
operator<= (buffer_view <T, Tag> lhs, buffer_view <T, Tag> rhs)
{
    return ! (rhs < lhs);
}

template <class T, class Tag>
inline
void
swap (buffer_view <T, Tag>& lhs, buffer_view <T, Tag>& rhs) noexcept
{
    std::swap (lhs.m_base, rhs.m_base);
    std::swap (lhs.m_size, rhs.m_size);
}

//------------------------------------------------------------------------------

template <
    class T,
    class Tag = buffer_view_default_tag
>
using const_buffer_view = buffer_view <
    std::add_const_t <T>, Tag>;

}

#endif
