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
#include <beast/http/message.h>
#include <beast/http/parser.h>
#include <beast/module/core/core.h>
#include <beast/module/asio/basics/SharedArg.h>
#include <beast/module/asio/http/HTTPRequestParser.h>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/streambuf.hpp>
#include <chrono>
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
    typedef std::chrono::system_clock clock_type;

    enum
    {
        // Size of our receive buffer
        bufferSize = 2048,

        // Largest HTTP request allowed
        maxRequestBytes = 32 * 1024,

        // Max seconds without completing a message
        timeoutSeconds = 30
        //timeoutSeconds = 3

    };

    beast::Journal journal_;
    ServerImpl& server_;
    std::string id_;
    boost::asio::io_service::strand strand_;
    boost::asio::deadline_timer timer_;
    std::unique_ptr <MultiSocket> socket_;

    boost::asio::streambuf read_buf_;
    beast::http::message message_;
    beast::http::parser parser_;
    int pending_writes_;
    bool closed_;
    bool finished_;
    bool callClose_;
    std::shared_ptr <Peer> detach_ref_;
    boost::optional <boost::asio::io_service::work> work_;

    boost::system::error_code ec_;
    std::atomic <int> detached_;

    clock_type::time_point when_;
    std::string when_str_;
    int request_count_;
    std::size_t bytes_in_;
    std::size_t bytes_out_;

    //--------------------------------------------------------------------------

public:
    Peer (ServerImpl& impl, Port const& port, beast::Journal journal);
    ~Peer ();

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
    accept();

private:
    void
    cancel();

    void
    failed (error_code const& ec);

    void
    start_timer();

    void
    async_write (SharedBuffer const& buf);

    //--------------------------------------------------------------------------
    //
    // Completion Handlers
    //

    void
    on_timer (error_code ec);

    void
    on_ssl_handshake (error_code ec);

    void
    on_read_request (error_code ec, std::size_t bytes_transferred);

    void
    on_write_response (error_code ec, std::size_t bytes_transferred,
        SharedBuffer const& buf);

    void
    on_close ();

    //--------------------------------------------------------------------------
    //
    // Session
    //

    beast::Journal
    journal() override
    {
        return server_.journal();
    }

    beast::IP::Endpoint
    remoteAddress() override
    {
        return from_asio (get_socket().remote_endpoint());
    }

    beast::http::message&
    message() override
    {
        return message_;
    }

    void
    write (void const* buffer, std::size_t bytes) override;

    void
    complete() override;

    void
    detach() override;

    void
    close (bool graceful) override;
};

}
}

#endif
