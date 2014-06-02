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

#ifndef BEAST_ASIO_WRAP_HANDLER_H_INCLUDED
#define BEAST_ASIO_WRAP_HANDLER_H_INCLUDED

#include <boost/asio/detail/handler_alloc_helpers.hpp>
#include <boost/asio/detail/handler_cont_helpers.hpp>
#include <boost/asio/detail/handler_invoke_helpers.hpp>

#include <beast/cxx14/type_traits.h> // <type_traits>
#include <utility>

namespace beast {
namespace asio {

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable: 4512) // assignment operator could not be generated
#endif

namespace detail {

/** A handler which wraps another handler using a specfic context.
    The handler is invoked with the same io_service execution guarantees
    as the provided context.
    @note A copy of Context is made.
*/
template <class Handler, class Context>
class wrapped_handler
{
private:
    Handler m_handler;
    Context m_context;
    bool m_continuation;

    // If this goes off, consider carefully what the intent is.
    static_assert (! std::is_reference <Handler>::value,
        "Handler should not be a reference type");

public:
    wrapped_handler (bool continuation, Handler&& handler, Context context)
        : m_handler (std::move (handler))
        , m_context (context)
        , m_continuation (continuation ? true :
            boost_asio_handler_cont_helpers::is_continuation (context))
    {
    }

    wrapped_handler (bool continuation, Handler const& handler, Context context)
        : m_handler (handler)
        , m_context (context)
        , m_continuation (continuation ? true :
            boost_asio_handler_cont_helpers::is_continuation (context))
    {
    }

    template <class... Args>
    void
    operator() (Args&&... args)
    {
        m_handler (std::forward <Args> (args)...);
    }

    template <class... Args>
    void
    operator() (Args&&... args) const
    {
        m_handler (std::forward <Args> (args)...);
    }

    template <class Function>
    friend
    void
    asio_handler_invoke (Function& f, wrapped_handler* h)
    {
        boost_asio_handler_invoke_helpers::
            invoke (f, h->m_context);
    }

    template <class Function>
    friend
    void
    asio_handler_invoke (Function const& f, wrapped_handler* h)
    {
        boost_asio_handler_invoke_helpers::
            invoke (f, h->m_context);
    }

    friend
    void*
    asio_handler_allocate (std::size_t size, wrapped_handler* h)
    {
        return boost_asio_handler_alloc_helpers::
            allocate (size, h->m_context);
    }

    friend
    void
    asio_handler_deallocate (void* p, std::size_t size, wrapped_handler* h)
    {
        boost_asio_handler_alloc_helpers::
            deallocate (p, size, h->m_context);
    }

    friend
    bool
    asio_handler_is_continuation (wrapped_handler* h)
    {
        return h->m_continuation;
    }
};

}

//------------------------------------------------------------------------------

// Tag for dispatching wrap_handler with is_continuation == true
enum continuation_t
{
    continuation
};

/** Returns a wrapped handler so it executes within another context.
    The handler is invoked with the same io_service execution guarantees
    as the provided context. The handler will be copied if necessary.
    @note A copy of Context is made.
*/
/** @{ */
template <class DeducedHandler, class Context>
detail::wrapped_handler <
    std::remove_reference_t <DeducedHandler>,
    Context
>
wrap_handler (DeducedHandler&& handler, Context const& context,
    bool continuation = false)
{
    typedef std::remove_reference_t <DeducedHandler> Handler;
    return detail::wrapped_handler <Handler, Context> (continuation,
        std::forward <DeducedHandler> (handler), context);
}

template <class DeducedHandler, class Context>
detail::wrapped_handler <
    std::remove_reference_t <DeducedHandler>,
    Context
>
wrap_handler (continuation_t, DeducedHandler&& handler,
    Context const& context)
{
    typedef std::remove_reference_t <DeducedHandler> Handler;
    return detail::wrapped_handler <Handler, Context> (true,
        std::forward <DeducedHandler> (handler), context);
}
/** @} */

}
}

#endif
