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

#ifndef BEAST_ASIO_HANDLER_ALLOC_H_INCLUDED
#define BEAST_ASIO_HANDLER_ALLOC_H_INCLUDED

#include <boost/asio/detail/handler_alloc_helpers.hpp>
#include <cstdlib>
#include <memory>
#include <type_traits>
#include <utility>

namespace beast {

// Guidance from
// http://howardhinnant.github.io/allocator_boilerplate.html

template <class T, class Handler>
class handler_alloc
{
private:
    // We want a partial template specialization as a friend
    // but that isn't allowed so we friend all versions. This
    // should produce a compile error if Handler is not
    // constructible from H.
    //
    template <class U, class H>
    friend class handler_alloc;

    Handler h_;

public:
    using value_type = T;
    using is_always_equal = std::true_type;

    handler_alloc() = delete;
    handler_alloc(handler_alloc&&) = default;
    handler_alloc(handler_alloc const&) = default;
    handler_alloc& operator=(handler_alloc&&) = default;
    handler_alloc& operator=(handler_alloc const&) = default;

    explicit
    handler_alloc(Handler&& h)
        : h_(std::move(h))
    {
    }

    explicit
    handler_alloc(Handler const& h)
        : h_(h)
    {
    }

    template<class U>
    handler_alloc(
            handler_alloc<U, Handler>&& other)
        : h_(std::move(other.h_))
    {
    }

    template<class U>
    handler_alloc(
            handler_alloc<U, Handler> const& other)
        : h_(other.h_)
    {
    }

    value_type*
    allocate(std::ptrdiff_t n)
    {
        auto const size = n * sizeof(T);
        return static_cast<value_type*>(
            boost_asio_handler_alloc_helpers::allocate(
                size, h_));
    }

    void
    deallocate(value_type* p, std::ptrdiff_t n)
    {
        auto const size = n * sizeof(T);
        boost_asio_handler_alloc_helpers::deallocate(
            p, size, h_);
    }

#ifdef _MSC_VER
    // Work-around for MSVC not using allocator_traits
    // in the implementation of shared_ptr
    //
    void
    destroy(T* t)
    {
        t->~T();
    }
#endif

    template<class U>
    friend
    bool
    operator==(handler_alloc const& lhs,
        handler_alloc<U, Handler> const& rhs)
    {
        return true;
    }

    template<class U>
    friend
    bool
    operator!=(handler_alloc const& lhs,
        handler_alloc<U, Handler> const& rhs)
    {
        return !(lhs == rhs);
    }
};

template <class T, class Handler>
class no_handler_alloc :
    public std::allocator<T>
{
private:
    using alloc_type = std::allocator<T>;

    // We want a partial template specialization as a friend
    // but that isn't allowed so we friend all versions. This
    // should produce a compile error if Handler is not
    // constructible from H.
    //
    template <class U, class H>
    friend class no_handler_alloc;

public:
    using value_type = T;
    using pointer = value_type*;
    using reference = value_type&;
    using const_pointer = value_type const*;
    using const_reference = value_type const&;
    using difference_type = std::ptrdiff_t;
    using size_type = std::size_t;

    template <class U>
    struct rebind
    {
    public:
        using other = no_handler_alloc<U, Handler>;
    };

    no_handler_alloc() = delete;
    no_handler_alloc(no_handler_alloc&&) = default;
    no_handler_alloc(no_handler_alloc const&) = default;
    no_handler_alloc& operator=(no_handler_alloc&&) = default;
    no_handler_alloc& operator=(no_handler_alloc const&) = default;

    no_handler_alloc(Handler&&)
    {
    }

    no_handler_alloc(Handler const&)
    {
    }

    template<class U>
    no_handler_alloc(
        no_handler_alloc<U, Handler>&& other)
    {
    }

    template<class U>
    no_handler_alloc(
        no_handler_alloc<U, Handler> const& other)
    {
    }

#ifdef _MSC_VER
    // Work-around for MSVC not using allocator_traits
    // in the implementation of shared_ptr
    //
    void
    destroy(T* t)
    {
        t->~T();
    }
#endif

    template<class U>
    friend
    bool
    operator==(no_handler_alloc const& lhs,
        no_handler_alloc<U, Handler> const& rhs)
    {
        return true;
    }

    template<class U>
    friend
    bool
    operator!=(no_handler_alloc const& lhs,
        no_handler_alloc<U, Handler> const& rhs)
    {
        return !(lhs == rhs);
    }
};

//------------------------------------------------------------------------------

template<class Handler>
class temp_buffer
{
    Handler& h_;
    std::size_t n_ = 0;
    std::uint8_t* p_ = nullptr;

public:
    explicit
    temp_buffer(Handler& h)
        : h_(h)
    {
    }

    ~temp_buffer()
    {
        if(p_)
            dealloc();
    }

    operator
    boost::asio::const_buffer() const
    {
        return boost::asio::const_buffer{p_, n_};
    }

    operator
    boost::asio::mutable_buffer() const
    {
        return boost::asio::mutable_buffer{p_, n_};
    }

    std::uint8_t*
    data() const
    {
        return p_;
    }

    std::size_t
    size()
    {
        return n_;
    }

    boost::asio::mutable_buffers_1
    buffers() const
    {
        return boost::asio::mutable_buffers_1{
            p_, n_};
    }

    void
    alloc(std::size_t size)
    {
        if(n_ != size)
        {
            if(p_)
                dealloc();
            n_ = size;
            if(n_ > 0)
                p_ = reinterpret_cast<std::uint8_t*>(
                    boost_asio_handler_alloc_helpers::
                        allocate(n_, h_));
        }
    }

    void
    dealloc()
    {
        boost_asio_handler_alloc_helpers::
            deallocate(p_, n_, h_);
        p_ = nullptr;
        n_ = 0;
    }
};

} // beast

#endif
