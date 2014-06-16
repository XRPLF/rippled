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

#include <beast/asio/wrap_handler.h>
#include <beast/asio/placeholders.h>

namespace ripple {
namespace PeerFinder {

class CheckerImp
    : public Checker
    , private beast::Thread
    , private beast::LeakChecked <CheckerImp>
{
private:
    class Request;

    struct State
    {
        beast::List <Request> list;
    };

    typedef beast::SharedData <State> SharedState;

    SharedState m_state;
    boost::asio::io_service m_io_service;
    boost::optional <boost::asio::io_service::work> m_work;

    //--------------------------------------------------------------------------

    class Request
        : public beast::SharedObject
        , public beast::List <Request>::Node
        , private beast::LeakChecked <Request>
    {
    public:
        typedef beast::SharedPtr <Request>         Ptr;
        typedef boost::asio::ip::tcp        Protocol;
        typedef boost::system::error_code   error_code;
        typedef Protocol::socket            socket_type;
        typedef Protocol::endpoint          endpoint_type;

        CheckerImp& m_owner;
        boost::asio::io_service& m_io_service;
        beast::IP::Endpoint m_address;
        beast::asio::shared_handler <void (Result)> m_handler;
        socket_type m_socket;
        boost::system::error_code m_error;
        bool m_canAccept;

        Request (CheckerImp& owner, boost::asio::io_service& io_service,
            beast::IP::Endpoint const& address, beast::asio::shared_handler <
                void (Result)> const& handler)
            : m_owner (owner)
            , m_io_service (io_service)
            , m_address (address)
            , m_handler (handler)
            , m_socket (m_io_service)
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
            m_io_service.wrap (m_handler) (result);

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
        m_io_service.run ();
    }

public:
    CheckerImp ()
        : Thread ("PeerFinder::Checker")
        , m_work (boost::in_place (std::ref (m_io_service)))
    {
        startThread ();
    }

    ~CheckerImp ()
    {
        // cancel pending i/o
        cancel();

        // destroy the io_service::work object
        m_work = boost::none;

        // signal and wait for the thread to exit gracefully
        stopThread ();
    }

    void cancel ()
    {
        SharedState::Access state (m_state);
        for (beast::List <Request>::iterator iter (state->list.begin());
            iter != state->list.end(); ++iter)
            iter->cancel();
    }

    void async_test (beast::IP::Endpoint const& endpoint,
        beast::asio::shared_handler <void (Result)> handler)
    {
        new Request (*this, m_io_service, endpoint, handler);
    }
};

//------------------------------------------------------------------------------

Checker* Checker::New ()
{
    return new CheckerImp;
}

}
}
