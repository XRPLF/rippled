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

#ifndef BEAST_ASIO_WRAPHANDLER_H_INCLUDED
#define BEAST_ASIO_WRAPHANDLER_H_INCLUDED

namespace beast {
namespace detail {

// Wrapper returned by wrapHandler, calls the Handler in the given Context
//
template <typename Handler, typename Context>
class WrappedHandler
{
public:
    typedef void result_type; // for result_of

    WrappedHandler (Handler& handler, Context const& context)
    : m_handler (handler)
    , m_context (context)
    {
    }

    WrappedHandler (Handler const& handler, Context const& context)
    : m_handler (handler)
    , m_context (context)
    {
    }

#if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    WrappedHandler (WrappedHandler const& other)
        : m_handler (other.m_handler)
        , m_context (other.m_context)
    {
    }

    WrappedHandler (BEAST_MOVE_ARG(WrappedHandler) other)
        : m_handler (BEAST_MOVE_CAST(Handler)(other.m_handler))
        , m_context (BEAST_MOVE_CAST(Context)(other.m_context))
    {
    }
#endif

    Handler& handler()
        { return m_handler; }

    Handler const& handler() const
        { return m_handler; }

    Context& context()
        { return m_context; }

    Context const& context() const
        { return m_context; }

    void operator() ()
        { m_handler(); }

    void operator() () const
        { m_handler(); }

    template <class P1>
    void operator() (P1 const& p1)
        { m_handler(p1); }

    template <class P1>
    void operator() (P1 const& p1) const
        { m_handler(p1); }

    template <class P1, class P2>
    void operator() (P1 const& p1, P2 const& p2)
        { m_handler(p1, p2); }

    template <class P1, class P2>
    void operator() (P1 const& p1, P2 const& p2) const
        { m_handler(p1, p2); }

    template <class P1, class P2, class P3>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3)
        { m_handler(p1, p2, p3); }

    template <class P1, class P2, class P3>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3) const
        { m_handler(p1, p2, p3); }

    template <class P1, class P2, class P3, class P4>
    void operator() 
        (P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4)
        { m_handler(p1, p2, p3, p4); }

    template <class P1, class P2, class P3, class P4>
    void operator() 
        (P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4) const
        { m_handler(p1, p2, p3, p4); }

    template <class P1, class P2, class P3,
              class P4, class P5>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3,
                     P4 const& p4, P5 const& p5)
        { m_handler(p1, p2, p3, p4, p5); }

    template <class P1, class P2, class P3,
              class P4, class P5>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3,
                     P4 const& p4, P5 const& p5) const
        { m_handler(p1, p2, p3, p4, p5); }

    template <class P1, class P2, class P3,
              class P4, class P5, class P6>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3,
                     P4 const& p4, P5 const& p5, P6 const& p6)
        { m_handler(p1, p2, p3, p4, p5, p6); }

    template <class P1, class P2, class P3,
              class P4, class P5, class P6>
    void operator() (P1 const& p1, P2 const& p2, P3 const& p3,
                     P4 const& p4, P5 const& p5, P6 const& p6) const
        { m_handler(p1, p2, p3, p4, p5, p6); }

private:
    Handler m_handler;
    Context m_context;
};

//------------------------------------------------------------------------------

template <typename Handler, typename Context>
void* asio_handler_allocate (std::size_t size,
    WrappedHandler <Handler, Context>* this_handler)
{
    return boost_asio_handler_alloc_helpers::allocate(
        size, this_handler->context());
}

template <typename Handler, typename Context>
void asio_handler_deallocate (void* pointer, std::size_t size,
    WrappedHandler <Handler, Context>* this_handler)
{
    boost_asio_handler_alloc_helpers::deallocate(
        pointer, size, this_handler->context());
}

template <typename Handler, typename Context>
bool asio_handler_is_continuation(
    WrappedHandler <Handler, Context>* this_handler)
{
#if BEAST_ASIO_HAS_CONTINUATION_HOOKS
    return boost_asio_handler_cont_helpers::is_continuation(
        this_handler->handler());
#else
    return false;
#endif
}

template <typename Function, typename Handler, typename Context>
void asio_handler_invoke (Function& function,
    WrappedHandler <Handler, Context>* handler)
{
    boost_asio_handler_invoke_helpers::invoke(
        function, handler->context());
}

template <typename Function, typename Handler, typename Context>
void asio_handler_invoke (Function const& function,
    WrappedHandler <Handler, Context>* handler)
{
    boost_asio_handler_invoke_helpers::invoke(
        function, handler->context());
}

}

//------------------------------------------------------------------------------

/** Returns a handler that calls Handler using Context hooks.
    This is useful when implementing composed asynchronous operations that
    need to call their own intermediate handlers before issuing the final
    completion to the original handler.
*/
template <typename Handler, typename Context>
detail::WrappedHandler <Handler, Context>
    wrapHandler (
        BEAST_MOVE_ARG(Handler) handler,
            BEAST_MOVE_ARG(Context) context)
{
    return detail::WrappedHandler <Handler, Context> (
        BEAST_MOVE_CAST(Handler)(handler),
            BEAST_MOVE_CAST(Context)(context));
}

}

#endif
