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

#ifndef RIPPLE_HTTP_DOOR_H_INCLUDED
#define RIPPLE_HTTP_DOOR_H_INCLUDED

#include <beast/asio/placeholders.h>

namespace ripple {
namespace HTTP {

/** A listening socket. */
class Door
    : public beast::SharedObject
    , public beast::asio::AsyncObject <Door>
    , public beast::List <Door>::Node
    , public beast::LeakChecked <Door>
{
public:
    typedef beast::SharedPtr <Door> Ptr;

    ServerImpl& m_impl;
    acceptor m_acceptor;
    Port m_port;

    Door (ServerImpl& impl, Port const& port)
        : m_impl (impl)
        , m_acceptor (m_impl.get_io_service(), to_asio (port))
        , m_port (port)
    {
        m_impl.add (*this);

        error_code ec;

        m_acceptor.set_option (acceptor::reuse_address (true), ec);
        if (ec)
        {
            m_impl.journal().error <<
                "Error setting acceptor socket option: " << ec.message();
        }

        if (! ec)
        {
            m_impl.journal().info << "Bound to endpoint " <<
                to_string (m_acceptor.local_endpoint());

            async_accept();
        }
        else
        {
            m_impl.journal().error << "Error binding to endpoint " <<
                to_string (m_acceptor.local_endpoint()) <<
                ", '" << ec.message() << "'";
        }
    }

    ~Door ()
    {
        m_impl.remove (*this);
    }

    Port const& port () const
    {
        return m_port;
    }

    void cancel ()
    {
        m_acceptor.cancel();
    }

    void failed (error_code ec)
    {
    }

    void asyncHandlersComplete ()
    {
    }

    void async_accept ()
    {
        Peer* peer (new Peer (m_impl, m_port));
        m_acceptor.async_accept (peer->get_socket(), std::bind (
            &Door::handle_accept, Ptr(this),
                beast::asio::placeholders::error,
                    Peer::Ptr (peer), CompletionCounter (this)));
    }

    void handle_accept (error_code ec, Peer::Ptr peer, CompletionCounter)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        if (ec)
        {
            m_impl.journal().error << "Accept failed: " << ec.message();
            return;
        }

        async_accept();

        peer->handle_accept();
    }
};

}
}

#endif
