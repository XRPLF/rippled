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

#ifndef BEAST_TRANSFERCALL_H_INCLUDED
#define BEAST_TRANSFERCALL_H_INCLUDED

//  Meets these requirements
//
//      ReadHandler
//      http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ReadHandler.html
//
//      WriteHandler
//      http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/WriteHandler.html
//
//      BUfferedHandshakeHandler
//      http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/BufferedHandshakeHandler.html
//
class TransferCall
{
public:
    typedef void result_type;

    template <class Handler>
    TransferCall (BOOST_ASIO_MOVE_ARG(Handler) handler)
        : m_call (new CallType <Handler> (BOOST_ASIO_MOVE_CAST(Handler)(handler)))
    {
    }

    TransferCall (TransferCall const& other)
        : m_call (other.m_call)
    { 
    }

    void operator() (boost::system::error_code const& ec, std::size_t bytes_transferred)
    {
        (*m_call) (ec, bytes_transferred);
    }

private:
    struct Call : SharedObject, LeakChecked <Call>
    {
        virtual void operator() (boost::system::error_code const&, std::size_t) = 0;
    };

    template <class Handler>
    struct CallType : Call
    {
        CallType (BOOST_ASIO_MOVE_ARG(Handler) handler)
            : m_handler (handler)
        {
        }

        void operator() (boost::system::error_code const& ec, std::size_t bytes_transferred)
        {
            m_handler (ec, bytes_transferred);
        }

        Handler m_handler;
    };

private:
    SharedObjectPtr <Call> m_call;
};

#endif
