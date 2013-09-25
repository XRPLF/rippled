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

#ifndef BEAST_ASIO_ASYNC_SHAREDHANDLERALLOCATOR_H_INCLUDED
#define BEAST_ASIO_ASYNC_SHAREDHANDLERALLOCATOR_H_INCLUDED

/** Custom Allocator using the allocation hooks from the Handler.

    This class is compatible with std::allocator and can be used in any
    boost interface which takes a template parameter of type Allocator.
    This includes boost::function and especially boost::asio::streambuf
    and relatives. This is vastly more efficient in a variety of situations
    especially during an upcall and when using stackful coroutines.

    The Allocator holds a reference to the underlying SharedHandler. The
    SharedHandler will not be destroyed as long as any Allocator is still
    using it.
*/
template <typename T>
struct SharedHandlerAllocator
{
    typedef T value_type;
    typedef T* pointer;
    typedef T& reference;
    typedef T const* const_pointer;
    typedef T const& const_reference;
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;

    SharedHandlerAllocator (SharedHandler* handler) noexcept
        : m_ptr (handler)
    {
    }

    SharedHandlerAllocator (SharedHandlerPtr const& handler) noexcept
        : m_ptr (handler)
    {
    }

    template <typename U>
    SharedHandlerAllocator (SharedHandlerAllocator <U> const& other)
        : m_ptr (other.m_ptr)
    {
    }

    template <typename U>
    struct rebind
    {
        typedef SharedHandlerAllocator <U> other;
    };

    pointer address (reference x) const
    {
        return &x;
    }

    const_pointer address (const_reference x) const
    {
        return &x;
    }

    pointer allocate (size_type n) const
    {
        size_type const bytes = n * sizeof (value_type);
        return static_cast <pointer> (m_ptr->allocate (bytes));
    }

    void deallocate (pointer p, size_type n) const
    {
        size_type const bytes = n * sizeof (value_type);
        m_ptr->deallocate (p, bytes);
    }

    size_type max_size () const noexcept
    {
        return std::numeric_limits <size_type>::max () / sizeof (value_type);
    }

    void construct (pointer p, const_reference val) const
    {
        new ((void *)p) value_type (val);
    }

    void destroy (pointer p) const
    {
        p->~value_type ();
    }

private:
    template <typename>
    friend struct SharedHandlerAllocator;
    friend class SharedHandler;

    SharedHandlerPtr m_ptr;
};

//------------------------------------------------------------------------------

#if 0
template <typename Function>
void SharedHandler::invoke (BOOST_ASIO_MOVE_ARG(Function) f)
{
    // The allocator will hold a reference to the SharedHandler
    // so that we can safely destroy the function object.
    invoked_type invoked (BOOST_ASIO_MOVE_CAST(Function)(f),
        SharedHandlerAllocator <char> (this));
    invoke (invoked);
}
#endif

#endif
