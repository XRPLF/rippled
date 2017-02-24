//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HANDLER_HELPERS_HPP
#define BEAST_HANDLER_HELPERS_HPP

#include <boost/asio/handler_alloc_hook.hpp>
#include <boost/asio/handler_continuation_hook.hpp>
#include <boost/asio/handler_invoke_hook.hpp>
#include <memory>

/*  Calls to:

    * asio_handler_allocate
    * asio_handler_deallocate
    * asio_handler_invoke
    * asio_handler_is_continuation

    must be made from a namespace that does not
    contain overloads of this function. The beast_asio_helpers
    namespace is defined here for that purpose.
*/

namespace beast_asio_helpers {

/// Allocation function for handlers.
template <class Handler>
inline
void*
allocate(std::size_t s, Handler& handler)
{
#if !defined(BOOST_ASIO_HAS_HANDLER_HOOKS)
    return ::operator new(s);
#else
    using boost::asio::asio_handler_allocate;
    return asio_handler_allocate(s, std::addressof(handler));
#endif
}

/// Deallocation function for handlers.
template<class Handler>
inline
void
deallocate(void* p, std::size_t s, Handler& handler)
{
#if !defined(BOOST_ASIO_HAS_HANDLER_HOOKS)
    ::operator delete(p);
#else
    using boost::asio::asio_handler_deallocate;
    asio_handler_deallocate(p, s, std::addressof(handler));
#endif
}

/// Invoke function for handlers.
template<class Function, class Handler>
inline
void
invoke(Function& function, Handler& handler)
{
#if !defined(BOOST_ASIO_HAS_HANDLER_HOOKS)
    Function tmp(function);
    tmp();
#else
    using boost::asio::asio_handler_invoke;
    asio_handler_invoke(function, std::addressof(handler));
#endif
}

/// Invoke function for handlers.
template<class Function, class Handler>
inline
void
invoke(Function const& function, Handler& handler)
{
#if !defined(BOOST_ASIO_HAS_HANDLER_HOOKS)
    Function tmp(function);
    tmp();
#else
    using boost::asio::asio_handler_invoke;
    asio_handler_invoke(function, std::addressof(handler));
#endif
}

/// Returns true if handler represents a continuation of the asynchronous operation
template<class Handler>
inline
bool
is_continuation(Handler& handler)
{
#if !defined(BOOST_ASIO_HAS_HANDLER_HOOKS)
    return false;
#else
    using boost::asio::asio_handler_is_continuation;
    return asio_handler_is_continuation(std::addressof(handler));
#endif
}

} // beast_asio_helpers

#endif
