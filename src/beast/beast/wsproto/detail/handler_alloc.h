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

#ifndef BEAST_WSPROTO_DETAIL_HANDLER_ALLOC_H_INCLUDED
#define BEAST_WSPROTO_DETAIL_HANDLER_ALLOC_H_INCLUDED

#include <boost/asio/detail/handler_alloc_helpers.hpp>
#include <cstdlib>
#include <utility>

namespace beast {
namespace wsproto {
namespace detail {

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
        using other = handler_alloc<U, Handler>;
    };

    handler_alloc() = delete;
    handler_alloc& operator=(handler_alloc const&) = delete;

    handler_alloc(Handler&& h)
        : h_(std::move(h))
    {
    }

    handler_alloc(Handler const& h)
        : h_(h)
    {
    }
    template<class U>
    handler_alloc(
        handler_alloc <U, Handler>&& other)
        : h_(std::move(other.h_))
    {
    }

    template<class U>
    handler_alloc(
        handler_alloc <U, Handler> const& other)
        : h_(other.h_)
    {
    }

    pointer
    allocate(std::ptrdiff_t n)
    {
        auto const size = n * sizeof(T);
        return static_cast<pointer>(
            boost_asio_handler_alloc_helpers::allocate(
                size, h_));
    }

    void
    deallocate(pointer p, std::ptrdiff_t n)
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

} // detail
} // wsproto
} // beast

#endif
