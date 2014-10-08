//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_PEERFINDER_CHECKERIMP_H_INCLUDED
#define RIPPLE_PEERFINDER_CHECKERIMP_H_INCLUDED

#include <ripple/peerfinder/impl/Checker.h>
#include <beast/asio/IPAddressConversion.h>
#include <beast/asio/placeholders.h>
#include <beast/asio/wrap_handler.h>
#include <beast/utility/LeakChecked.h>
#include <beast/smart_ptr/SharedObject.h>
#include <beast/smart_ptr/SharedPtr.h>
#include <beast/threads/Thread.h>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/optional.hpp>

namespace ripple {
namespace PeerFinder {

class CheckerImp : public Checker
{
private:
    class Request;

    struct State
    {
        beast::List <Request> list;
    };

    typedef beast::SharedData <State> SharedState;

    SharedState m_state;
    boost::asio::io_service io_service_;

    //--------------------------------------------------------------------------

    class Request
        : public beast::SharedObject
        , public beast::List <Request>::Node
        , private beast::LeakChecked <Request>
    {
    public:
        typedef beast::SharedPtr <Request>  Ptr;
        typedef boost::asio::ip::tcp        Protocol;
        typedef boost::system::error_code   error_code;
        typedef Protocol::socket            socket_type;
        typedef Protocol::endpoint          endpoint_type;

        CheckerImp& m_owner;
        boost::asio::io_service& io_service_;
        beast::IP::Endpoint m_address;
        beast::asio::shared_handler <void (Result)> m_handler;
        socket_type m_socket;
        boost::system::error_code m_error;
        bool m_canAccept;

        Request (CheckerImp& owner, boost::asio::io_service& io_service,
            beast::IP::Endpoint const& address, beast::asio::shared_handler <
                void (Result)> const& handler)
            : m_owner (owner)
            , io_service_ (io_service)
            , m_address (address)
            , m_handler (handler)
            , m_socket (io_service_)
            , m_canAccept (false)
        {
            m_owner.add (*this);

            m_socket.async_connect (beast::IPAddressConversion::to_asio_endpoint (
                m_address), beast::asio::wrap_handler (std::bind (
                    &Request::handle_connect, Ptr(this),
                        beast::asio::placeholders::error), m_handler));
        }

        ~Request ()
        {
            Result result;
            result.address = m_address;
            result.error = m_error;
            io_service_.wrap (m_handler) (result);

            m_owner.remove (*this);
        }

        void cancel ()
        {
            m_socket.cancel();
        }

        void handle_connect (boost::system::error_code ec)
        {
            m_error = ec;
            if (ec)
                return;

            m_canAccept = true;
        }
    };

    //--------------------------------------------------------------------------

    void add (Request& request)
    {
        SharedState::Access state (m_state);
        state->list.push_back (request);
    }

    void remove (Request& request)
    {
        SharedState::Access state (m_state);
        state->list.erase (state->list.iterator_to (request));
    }

    void run ()
    {
        io_service_.run ();
    }

public:
    CheckerImp (boost::asio::io_service& io_service)
    {
    }

    ~CheckerImp ()
    {
        // cancel pending i/o
        stop();
    }

    void stop()
    {
        SharedState::Access state (m_state);
        for (auto& c : state->list)
            c.cancel();
    }

    void async_test (beast::IP::Endpoint const& endpoint,
        beast::asio::shared_handler <void (Result)> handler)
    {
        new Request (*this, io_service_, endpoint, handler);
    }
};

}
}

#endif
