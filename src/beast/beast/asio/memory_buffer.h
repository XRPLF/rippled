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

#ifndef BEAST_ASIO_MEMORY_BUFFER_H_INCLUDED
#define BEAST_ASIO_MEMORY_BUFFER_H_INCLUDED

#include <beast/utility/empty_base_optimization.h>

#include <boost/asio/buffer.hpp>

#include <beast/utility/noexcept.h>
#include <cstddef>
#include <memory>
#include <type_traits>

namespace beast {
namespace asio {

template <
    class T,
    class Alloc = std::allocator <T>
>
class memory_buffer
    : private empty_base_optimization <Alloc>
{
private:
    static_assert (std::is_same <char, T>::value ||
                   std::is_same <unsigned char, T>::value,
        "memory_buffer only works with char and unsigned char");

    typedef empty_base_optimization <Alloc> Base;

    using AllocTraits = std::allocator_traits <Alloc>;

    T* m_base;
    std::size_t m_size;

public:
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;
    typedef T& reference;
    typedef T const& const_reference;
    typedef T* pointer;
    typedef T const* const_pointer;
    typedef Alloc allocator_type;
    typedef T* iterator;
    typedef T const* const_iterator;
    typedef std::reverse_iterator <iterator> reverse_iterator;
    typedef std::reverse_iterator <const_iterator> const_reverse_iterator;

    memory_buffer ()
        : m_base (nullptr)
        , m_size (0)
    {
    }

    memory_buffer (memory_buffer&& other)
        : Base (std::move (other))
        , m_base (other.m_base)
        , m_size (other.m_size)
    {
        other.m_base = nullptr;
        other.m_size = 0;
    }

    explicit memory_buffer (size_type n)
        : m_base (AllocTraits::allocate (Base::member(), n))
        , m_size (n)
    {
    }

    explicit memory_buffer (Alloc const& alloc)
        : Base (alloc)
        , m_base (nullptr)
        , m_size (0)
    {
    }

    memory_buffer (size_type n, Alloc const& alloc)
        : Base (alloc)
        , m_base (AllocTraits::allocate (Base::member(), n))
        , m_size (n)
    {
    }

    ~memory_buffer()
    {
        if (m_base != nullptr)
            AllocTraits::deallocate (Base::member(), m_base, m_size);
    }

    memory_buffer& operator= (memory_buffer const&) = delete;

    allocator_type
    get_allocator() const
    {
        return Base::member;
    }

    //
    // asio support
    //

    boost::asio::mutable_buffer
    buffer()
    {
        return boost::asio::mutable_buffer (
            data(), bytes());
    }

    boost::asio::const_buffer
    buffer() const
    {
        return boost::asio::const_buffer (
            data(), bytes());
    }

    boost::asio::mutable_buffers_1
    buffers()
    {
        return boost::asio::mutable_buffers_1 (
            data(), bytes());
    }

    boost::asio::const_buffers_1
    buffers() const
    {
        return boost::asio::const_buffers_1 (
            data(), bytes());
    }

    operator boost::asio::mutable_buffer()
    {
        return buffer();
    }

    operator boost::asio::const_buffer() const
    {
        return buffer();
    }

    operator boost::asio::mutable_buffers_1()
    {
        return buffers();
    }

    operator boost::asio::const_buffers_1() const
    {
        return buffers();
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

    size_type bytes() const
    {
        return m_size * sizeof(T);
    }

    //
    // Modifiers
    //

    template <class U, class A>
    friend
    void
    swap (memory_buffer <U, A>& lhs,
        memory_buffer <U, A>& rhs) noexcept;
};

//------------------------------------------------------------------------------

template <class T, class Alloc>
void
swap (memory_buffer <T, Alloc>& lhs,
    memory_buffer <T, Alloc>& rhs) noexcept
{
    std::swap (lhs.m_base, rhs.m_base);
    std::swap (lhs.m_size, rhs.m_size);
}

template <class T, class A1, class A2>
inline
bool
operator== (memory_buffer <T, A1> const& lhs,
            memory_buffer <T, A2> const& rhs)
{
    return std::equal (lhs.cbegin(), lhs.cend(),
        rhs.cbegin(), rhs.cend());
}

template <class T, class A1, class A2>
inline
bool
operator!= (memory_buffer <T, A1> const& lhs,
            memory_buffer <T, A2> const& rhs)
{
    return ! (lhs == rhs);
}

template <class T, class A1, class A2>
inline
bool
operator< (memory_buffer <T, A1> const& lhs,
           memory_buffer <T, A2> const& rhs)
{
    return std::lexicographical_compare (
        lhs.cbegin(), lhs.cend(), rhs.cbegin(), rhs.cend());
}

template <class T, class A1, class A2>
inline
bool
operator>= (memory_buffer <T, A1> const& lhs,
            memory_buffer <T, A2> const& rhs)
{
    return ! (lhs < rhs);
}

template <class T, class A1, class A2>
inline
bool
operator> (memory_buffer <T, A1> const& lhs,
           memory_buffer <T, A2> const& rhs)
{
    return rhs < lhs;
}

template <class T, class A1, class A2>
inline
bool
operator<= (memory_buffer <T, A1> const& lhs,
            memory_buffer <T, A2> const& rhs)
{
    return ! (rhs < lhs);
}

}
}

#endif
