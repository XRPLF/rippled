//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HANDLER_PTR_HPP
#define BEAST_HANDLER_PTR_HPP

#include <beast/core/detail/type_traits.hpp>
#include <atomic>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace beast {

/** A smart pointer container with associated completion handler.

    This is a smart pointer that retains shared ownership of an
    object through a pointer. Memory is managed using the allocation
    and deallocation functions associated with a completion handler,
    which is also stored in the object. The managed object is
    destroyed and its memory deallocated when one of the following
    happens:

    @li The function @ref invoke is called.

    @li The function @ref release_handler is called.

    @li The last remaining container owning the object is destroyed.

    Objects of this type are used in the implementation of
    composed operations. Typically the composed operation's shared
    state is managed by the @ref handler_ptr and an allocator
    associated with the final handler is used to create the managed
    object.

    @note The reference count is stored using a 16 bit unsigned
    integer. Making more than 2^16 copies of one object results
    in undefined behavior.

    @tparam T The type of the owned object.

    @tparam Handler The type of the completion handler.
*/
template<class T, class Handler>
class handler_ptr
{
    struct P
    {
        T* t;
        std::atomic<std::uint16_t> n;

        // There's no way to put the handler anywhere else
        // without exposing ourselves to race conditions
        // and all sorts of ugliness.
        // See:
        //  https://github.com/vinniefalco/Beast/issues/215
        Handler handler;

        template<class DeducedHandler, class... Args>
        P(DeducedHandler&& handler, Args&&... args);
    };

    P* p_;

public:
    /// The type of element this object stores
    using element_type = T;

    /// The type of handler this object stores
    using handler_type = Handler;

    /// Copy assignment (disallowed).
    handler_ptr& operator=(handler_ptr const&) = delete;

    /** Destructs the owned object if no more @ref handler_ptr link to it.

        If `*this` owns an object and it is the last @ref handler_ptr
        owning it, the object is destroyed and the memory deallocated
        using the associated deallocator.
    */
    ~handler_ptr();

    /** Move constructor.

        When this call returns, the moved-from container
        will have no owned object.
    */
    handler_ptr(handler_ptr&& other);

    /// Copy constructor
    handler_ptr(handler_ptr const& other);

    /** Construct a new @ref handler_ptr

        This creates a new @ref handler_ptr with an owned object
        of type `T`. The allocator associated with the handler will
        be used to allocate memory for the owned object. The constructor
        for the owned object will be called thusly:

        @code
            T(handler, std::forward<Args>(args)...)
        @endcode

        @param handler The handler to associate with the owned
        object. The argument will be moved.

        @param args Optional arguments forwarded to
        the owned object's constructor.
    */
    template<class... Args>
    handler_ptr(Handler&& handler, Args&&... args);

    /** Construct a new @ref handler_ptr

        This creates a new @ref handler_ptr with an owned object
        of type `T`. The allocator associated with the handler will
        be used to allocate memory for the owned object. The constructor
        for the owned object will be called thusly:

        @code
            T(handler, std::forward<Args>(args)...)
        @endcode

        @param handler The handler to associate with the owned
        object. The argument will be copied.

        @param args Optional arguments forwarded to
        the owned object's constructor.
    */
    template<class... Args>
    handler_ptr(Handler const& handler, Args&&... args);

    /// Returns a reference to the handler
    handler_type&
    handler() const
    {
        return p_->handler;
    }

    /// Returns `true` if `*this` owns an object.
    explicit
    operator bool() const
    {
        return p_ && p_->t;
    }

    /** Returns a pointer to the owned object.

        If `*this` owns an object, a pointer to the
        object is returned, else `nullptr` is returned.
    */
    T*
    get() const
    {
        return p_ ? p_->t : nullptr;
    }

    /// Return a reference to the owned object.
    T&
    operator*() const
    {
        return *p_->t;
    }

    /// Return a pointer to the owned object.
    T*
    operator->() const
    {
        return p_->t;
    }

    /** Release ownership of the handler

        If `*this` owns an object, it is first destroyed.

        @return The released handler.
    */
    handler_type
    release_handler();

    /** Invoke the handler in the owned object.

        This function invokes the handler in the owned object
        with a forwarded argument list. Before the invocation,
        the owned object is destroyed, satisfying the
        deallocation-before-invocation Asio guarantee. All
        instances of @ref handler_ptr which refer to the
        same owned object will be reset, including this instance.
    */
    template<class... Args>
    void
    invoke(Args&&... args);
};

} // beast

#include <beast/core/impl/handler_ptr.ipp>

#endif
