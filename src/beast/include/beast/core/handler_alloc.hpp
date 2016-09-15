//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HANDLER_ALLOC_HPP
#define BEAST_HANDLER_ALLOC_HPP

#include <boost/asio/detail/handler_alloc_helpers.hpp>
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
    of `Allocator` and can be used anywhere a `std::allocator` is
    accepted.

    @tparam T The type of objects allocated by the allocator.

    @tparam CompletionHandler The type of handler.

    @note Allocated memory is only valid until the handler is called. The
    caller is still responsible for freeing memory.
*/
#if GENERATING_DOCS
template<class T, class CompletionHandler>
class handler_alloc;
#else
template<class T, class CompletionHandler>
class handler_alloc
{
private:
    // We want a partial template specialization as a friend
    // but that isn't allowed so we friend all versions. This
    // should produce a compile error if CompletionHandler is not
    // constructible from H.
    //
    template<class U, class H>
    friend class handler_alloc;

    CompletionHandler h_;

public:
    using value_type = T;
    using is_always_equal = std::true_type;

    handler_alloc() = delete;
    handler_alloc(handler_alloc&&) = default;
    handler_alloc(handler_alloc const&) = default;
    handler_alloc& operator=(handler_alloc&&) = default;
    handler_alloc& operator=(handler_alloc const&) = default;

    /** Construct the allocator.

        The handler is moved or copied into the allocator.
    */
    explicit
    handler_alloc(CompletionHandler&& h)
        : h_(std::move(h))
    {
    }

    /** Construct the allocator.

        A copy of the handler is made.
    */
    explicit
    handler_alloc(CompletionHandler const& h)
        : h_(h)
    {
    }

    template<class U>
    handler_alloc(
            handler_alloc<U, CompletionHandler>&& other)
        : h_(std::move(other.h_))
    {
    }

    template<class U>
    handler_alloc(
            handler_alloc<U, CompletionHandler> const& other)
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
        handler_alloc<U, CompletionHandler> const& rhs)
    {
        return true;
    }

    template<class U>
    friend
    bool
    operator!=(handler_alloc const& lhs,
        handler_alloc<U, CompletionHandler> const& rhs)
    {
        return !(lhs == rhs);
    }
};
#endif

} // beast

#endif
