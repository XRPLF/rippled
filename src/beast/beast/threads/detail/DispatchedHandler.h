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

#ifndef BEAST_THREADS_DETAIL_DISPATCHEDHANDLER_H_INCLUDED
#define BEAST_THREADS_DETAIL_DISPATCHEDHANDLER_H_INCLUDED

#include <beast/threads/detail/BindHandler.h>

namespace beast {
namespace detail {

/** A wrapper that packages function call arguments into a dispatch. */
template <typename Dispatcher, typename Handler>
class DispatchedHandler
{
private:
    Dispatcher m_dispatcher;
    Handler m_handler;

public:
    typedef void result_type;

    DispatchedHandler (Dispatcher dispatcher, Handler& handler)
        : m_dispatcher (dispatcher)
        , m_handler (BEAST_MOVE_CAST(Handler)(handler))
    {
    }

#if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    DispatchedHandler (DispatchedHandler const& other)
        : m_dispatcher (other.m_dispatcher)
        , m_handler (other.m_handler)
    {
    }

    DispatchedHandler (DispatchedHandler&& other)
        : m_dispatcher (other.m_dispatcher)
        , m_handler (BEAST_MOVE_CAST(Handler)(other.m_handler))
    {
    }
#endif

    void operator()()
    {
        m_dispatcher.dispatch (m_handler);
    }

    void operator()() const
    {
        m_dispatcher.dispatch (m_handler);
    }

    template <class P1>
    void operator() (P1 const& p1)
    {
        m_dispatcher.dispatch (
            detail::bindHandler (m_handler,
                p1));
    }

    template <class P1>
    void operator() (P1 const& p1) const
    {
        m_dispatcher.dispatch (
            detail::bindHandler (m_handler,
                p1));
    }

    template <class P1, class P2>
    void operator() (P1 const& p1, P2 const& p2)
    {
        m_dispatcher.dispatch (
            detail::bindHandler (m_handler,
                p1, p2));
    }

    template <class P1, class P2>
    void operator() (P1 const& p1, P2 const& p2) const
    {
        m_dispatcher.dispatch (
            detail::bindHandler (m_handler,
                p1, p2));
    }

    template <class P1, class P2, class P3>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3)
    {
        m_dispatcher.dispatch (
            detail::bindHandler (m_handler,
                p1, p2, p3));
    }

    template <class P1, class P2, class P3>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3) const
    {
        m_dispatcher.dispatch (
            detail::bindHandler (m_handler,
                p1, p2, p3));
    }

    template <class P1, class P2, class P3, class P4>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3,  P4 const& p4)
    {
        m_dispatcher.dispatch (
            detail::bindHandler (m_handler,
                p1, p2, p3, p4));
    }

    template <class P1, class P2, class P3, class P4>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3,  P4 const& p4) const
    {
        m_dispatcher.dispatch (
            detail::bindHandler (m_handler,
                p1, p2, p3, p4));
    }

    template <class P1, class P2, class P3, class P4, class P5>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3, 
                     P4 const& p4, P5 const& p5)
    {
        m_dispatcher.dispatch (
            detail::bindHandler (m_handler,
                p1, p2, p3, p4, p5));
    }

    template <class P1, class P2, class P3, class P4, class P5>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3, 
                     P4 const& p4, P5 const& p5) const
    {
        m_dispatcher.dispatch (
            detail::bindHandler (m_handler,
                p1, p2, p3, p4, p5));
    }

    template <class P1, class P2, class P3, class P4, class P5, class P6>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3, 
                     P4 const& p4, P5 const& p5, P6 const& p6)
    {
        m_dispatcher.dispatch (
            detail::bindHandler (m_handler,
                p1, p2, p3, p4, p5, p6));
    }

    template <class P1, class P2, class P3, class P4, class P5, class P6>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3, 
                     P4 const& p4, P5 const& p5, P6 const& p6) const
    {
        m_dispatcher.dispatch (
            detail::bindHandler (m_handler,
                p1, p2, p3, p4, p5, p6));
    }
};

}
}

#endif
