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

#ifndef RIPPLE_HTTP_PEER_H_INCLUDED
#define RIPPLE_HTTP_PEER_H_INCLUDED

#include <ripple/http/Server.h>
#include <ripple/http/Session.h>
#include <ripple/http/impl/Types.h>
#include <ripple/http/impl/ServerImpl.h>
#include <ripple/common/MultiSocket.h>
#include <beast/asio/placeholders.h>
#include <beast/module/core/core.h>
#include <beast/module/asio/basics/SharedArg.h>
#include <beast/module/asio/http/HTTPRequestParser.h>
#include <functional>
#include <memory>

namespace ripple {
namespace HTTP {

// Holds the copy of buffers being sent
// VFALCO TODO Replace with std::shared_ptr<std::string>
//
typedef beast::asio::SharedArg <std::string> SharedBuffer;

/** Represents an active connection. */
class Peer
    : public std::enable_shared_from_this <Peer>
    , public Session
    , public beast::List <Peer>::Node
    , public beast::LeakChecked <Peer>
{
private:
    enum
    {
        // Size of our receive buffer
        bufferSize = 8192,

        // Largest HTTP request allowed
        maxRequestBytes = 32 * 1024,

        // Max seconds without receiving a byte
        dataTimeoutSeconds = 10,

        // Max seconds without completing the request
        requestTimeoutSeconds = 30

    };

    ServerImpl& impl_;
    boost::asio::io_service::strand strand_;
    boost::asio::deadline_timer data_timer_;
    boost::asio::deadline_timer request_timer_;
    std::unique_ptr <MultiSocket> socket_;

    // VFALCO TODO Use c++11
    beast::MemoryBlock buffer_;

    beast::HTTPRequestParser parser_;
    int writesPending_;
    bool closed_;
    bool callClose_;
    std::shared_ptr <Peer> detach_ref_;
    boost::optional <boost::asio::io_service::work> work_;
    int errorCode_;
    std::atomic <int> detached_;

    //--------------------------------------------------------------------------

public:
    Peer (ServerImpl& impl, Port const& port);
    ~Peer ();

private:
    //--------------------------------------------------------------------------
    //
    // Session
    //

    beast::Journal
    journal()
    {
        return impl_.journal();
    }

    beast::IP::Endpoint
    remoteAddress()
    {
        return from_asio (get_socket().remote_endpoint());
    }

    bool
    headersComplete()
    {
        return parser_.headersComplete();
    }

    beast::HTTPHeaders
    headers()
    {
        return beast::HTTPHeaders (parser_.fields());
    }

    beast::SharedPtr <beast::HTTPRequest> const&
    request()
    {
        return parser_.request();
    }

    std::string
    content();

    void
    write (void const* buffer, std::size_t bytes);

    void
    detach ();

    void
    close ();

    //--------------------------------------------------------------------------
    //
    // Completion Handlers
    //

    void
    handle_handshake (error_code ec);

    void
    handle_data_timer (error_code ec);

    void
    handle_request_timer (error_code ec);

    void
    handle_write (error_code ec, std::size_t bytes_transferred,
        SharedBuffer const& buf);

    void
    handle_read (error_code ec, std::size_t bytes_transferred);

    void
    handle_headers ();

    void
    handle_request ();

    void
    handle_close ();

public:
    //--------------------------------------------------------------------------
    //
    // Peer
    //
    socket&
    get_socket()
    {
        return socket_->this_layer<socket>();
    }

    Session&
    session ()
    {
        return *this;
    }

    void
    accept ();

    void
    cancel ();

    void
    failed (error_code const& ec);

    void
    async_read_some ();

    void async_write (SharedBuffer const& buf);
};

}
}

#endif
