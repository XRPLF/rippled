//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HANDLER_ALLOC_HPP
#define BEAST_HANDLER_ALLOC_HPP

#include <beast/config.hpp>
#include <beast/core/handler_helpers.hpp>
#include <cstdlib>
#include <memory>
#include <type_traits>
#include <utility>

namespace beast {

// Guidance from
// http://howardhinnant.github.io/allocator_boilerplate.html

/** An allocator that uses handler customizations.

    This allocator uses the handler customizations `asio_handler_allocate`
    and `asio_handler_deallocate` to manage memory. It meets the requirements
    of @b Allocator and can be used anywhere a `std::allocator` is
    accepted.

    @tparam T The type of objects allocated by the allocator.

    @tparam Handler The type of handler.

    @note Memory allocated by this allocator must be freed before
    the handler is invoked or undefined behavior results. This behavior
    is described as the "deallocate before invocation" Asio guarantee.
*/
#if GENERATING_DOCS
template<class T, class Handler>
class handler_alloc;
#else
template<class T, class Handler>
class handler_alloc
{
private:
    // We want a partial template specialization as a friend
    // but that isn't allowed so we friend all versions. This
    // should produce a compile error if Handler is not
    // constructible from H.
    //
    template<class U, class H>
    friend class handler_alloc;

    Handler& h_;

public:
    using value_type = T;
    using is_always_equal = std::true_type;

    template<class U>
    struct rebind
    {
        using other = handler_alloc<U, Handler>;
    };

    handler_alloc() = delete;
    handler_alloc(handler_alloc&&) = default;
    handler_alloc(handler_alloc const&) = default;
    handler_alloc& operator=(handler_alloc&&) = default;
    handler_alloc& operator=(handler_alloc const&) = default;

    /** Construct the allocator.

        A reference of the handler is stored. The handler must
        remain valid for at least the lifetime of the allocator.
    */
    explicit
    handler_alloc(Handler& h)
        : h_(h)
    {
    }

    /// Copy constructor
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
            beast_asio_helpers::allocate(
                size, h_));
    }

    void
    deallocate(value_type* p, std::ptrdiff_t n)
    {
        auto const size = n * sizeof(T);
        beast_asio_helpers::deallocate(
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
        return ! (lhs == rhs);
    }
};
#endif

} // beast

#endif
