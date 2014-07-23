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

#ifndef BEAST_ASIO_BIND_HANDLER_H_INCLUDED
#define BEAST_ASIO_BIND_HANDLER_H_INCLUDED

#include <boost/asio/detail/handler_alloc_helpers.hpp>
#include <boost/asio/detail/handler_cont_helpers.hpp>
#include <boost/asio/detail/handler_invoke_helpers.hpp>

#include <functional>
#include <beast/cxx14/type_traits.h> // <type_traits>
#include <beast/cxx14/utility.h> // <utility>

namespace beast {
namespace asio {

namespace detail {

/** Nullary handler that calls Handler with bound arguments.
    The rebound handler provides the same io_service execution
    guarantees as the original handler.
*/
template <class DeducedHandler, class... Args>
class bound_handler
{
private:
    typedef std::tuple <std::decay_t <Args>...> args_type;

    std::decay_t <DeducedHandler> m_handler;
    args_type m_args;

    template <class Handler, class Tuple, std::size_t... S>
    static void invoke (Handler& h, Tuple& args,
        std::index_sequence <S...>)
    {
        h (std::get <S> (args)...);
    }

public:
    typedef void result_type;

    explicit
    bound_handler (DeducedHandler&& handler, Args&&... args)
        : m_handler (std::forward <DeducedHandler> (handler))
        , m_args (std::forward <Args> (args)...)
    {
    }

    void
    operator() ()
    {
        invoke (m_handler, m_args,
            std::index_sequence_for <Args...> ());
    }

    void
    operator() () const
    {
        invoke (m_handler, m_args,
            std::index_sequence_for <Args...> ());
    }

    template <class Function>
    friend
    void
    asio_handler_invoke (Function& f, bound_handler* h)
    {
        boost_asio_handler_invoke_helpers::
            invoke (f, h->m_handler);
    }

    template <class Function>
    friend
    void
    asio_handler_invoke (Function const& f, bound_handler* h)
    {
        boost_asio_handler_invoke_helpers::
            invoke (f, h->m_handler);
    }

    friend
    void*
    asio_handler_allocate (std::size_t size, bound_handler* h)
    {
        return boost_asio_handler_alloc_helpers::
            allocate (size, h->m_handler);
    }

    friend
    void
    asio_handler_deallocate (void* p, std::size_t size, bound_handler* h)
    {
        boost_asio_handler_alloc_helpers::
            deallocate (p, size, h->m_handler);
    }

    friend
    bool
    asio_handler_is_continuation (bound_handler* h)
    {
        return boost_asio_handler_cont_helpers::
            is_continuation (h->m_handler);
    }
};

}

//------------------------------------------------------------------------------

/** Binds parameters to a handler to produce a nullary functor.
    The returned handler provides the same io_service execution guarantees
    as the original handler. This is designed to use as a replacement for
    io_service::wrap, to ensure that the handler will not be invoked
    immediately by the calling function.
*/
template <class DeducedHandler, class... Args>
detail::bound_handler <DeducedHandler, Args...>
bind_handler (DeducedHandler&& handler, Args&&... args)
{
    return detail::bound_handler <DeducedHandler, Args...> (
        std::forward <DeducedHandler> (handler),
            std::forward <Args> (args)...);
}

}
}

//------------------------------------------------------------------------------

namespace std {

template <class Handler, class... Args>
void bind (beast::asio::detail::bound_handler <
    Handler, Args...>, ...)  = delete;

#if 0
template <class Handler, class... Args>
struct is_bind_expression <
    beast::asio::detail::bound_handler <Handler, Args...>
> : std::true_type
{
};
#endif

}

#endif
