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

#ifndef BEAST_COMPLETIONCALL_H_INCLUDED
#define BEAST_COMPLETIONCALL_H_INCLUDED

//  Meets these requirements:
//
//      CompletionHandler
//      http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/CompletionHandler.html
//
class CompletionCall
{
public:
    typedef void result_type;

    CompletionCall () noexcept
    {
    }

    // Construction from Handler with zero arguments
    //
    template <typename Handler>
    CompletionCall (BOOST_ASIO_MOVE_ARG(Handler) handler)
        : m_call (new CallType0 <Handler> (BOOST_ASIO_MOVE_CAST(Handler)(handler)))
    {
    }

    // Creates a CompletionHandler with one parameter bound to Handler
    // This can convert an ErrorCall to a CompletionCall, suitable
    // for calling asio::io_service::post()
    //
    template <typename Handler, typename P1>
    CompletionCall (BOOST_ASIO_MOVE_ARG(Handler) handler, P1 p1)
        : m_call (new CallType1 <Handler, P1> (BOOST_ASIO_MOVE_CAST(Handler)(handler), p1))
    {
    }

    // Creates a CompletionHandler with two parameters bound to Handler
    // This can convert a TransferCall to a CompletionCall, suitable
    // for calling asio::io_service::post()
    //
    template <typename Handler, typename P1, typename P2>
    CompletionCall (BOOST_ASIO_MOVE_ARG(Handler) handler, P1 p1, P2 p2)
        : m_call (new CallType2 <Handler, P1, P2> (BOOST_ASIO_MOVE_CAST(Handler)(handler), p1, p2))
    {
    }

    CompletionCall (CompletionCall const& other)
        : m_call (other.m_call)
    { 
    }

    bool isNull () const noexcept
    {
        return m_call == nullptr;
    }

    void operator() ()
    {
        (*m_call) ();
    }

private:
    struct Call : SharedObject, LeakChecked <Call>
    {
        virtual void operator() () = 0;
    };

    template <class Handler>
    struct CallType0 : Call
    {
        CallType0 (BOOST_ASIO_MOVE_ARG(Handler) handler)
            : m_handler (handler)
        {
        }

        void operator() ()
        {
            m_handler ();
        }

        Handler m_handler;
    };

    template <class Handler, typename P1>
    struct CallType1 : Call
    {
        CallType1 (BOOST_ASIO_MOVE_ARG(Handler) handler, P1 p1)
            : m_handler (handler)
            , m_p1 (p1)
        {
        }

        void operator() ()
        {
            m_handler (m_p1);
        }

        Handler m_handler;
        P1 m_p1;
    };

    template <class Handler, typename P1, typename P2>
    struct CallType2 : Call
    {
        CallType2 (BOOST_ASIO_MOVE_ARG(Handler) handler, P1 p1, P2 p2)
            : m_handler (handler)
            , m_p1 (p1)
            , m_p2 (p2)
        {
        }

        void operator() ()
        {
            m_handler (m_p1, m_p2);
        }

        Handler m_handler;
        P1 m_p1;
        P2 m_p2;
    };

private:
    SharedObjectPtr <Call> m_call;
};

#endif
