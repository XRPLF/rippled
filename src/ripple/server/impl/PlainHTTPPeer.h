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

#ifndef RIPPLE_SERVER_PLAINHTTPPEER_H_INCLUDED
#define RIPPLE_SERVER_PLAINHTTPPEER_H_INCLUDED

#include <ripple/server/impl/BaseHTTPPeer.h>
#include <memory>

namespace ripple {

class PlainHTTPPeer
    : public BaseHTTPPeer<PlainHTTPPeer>
    , public std::enable_shared_from_this <PlainHTTPPeer>
{
private:
    friend class BaseHTTPPeer<PlainHTTPPeer>;
    using socket_type = boost::asio::ip::tcp::socket;

    socket_type stream_;

public:
    template <class ConstBufferSequence>
    PlainHTTPPeer (Port const& port, Handler& handler,
        beast::Journal journal, endpoint_type remote_address,
            ConstBufferSequence const& buffers, socket_type&& socket);

    void
    run();

private:
    void
    do_request();

    void
    do_close();
};

//------------------------------------------------------------------------------

template <class ConstBufferSequence>
PlainHTTPPeer::PlainHTTPPeer (Port const& port, Handler& handler,
    beast::Journal journal, endpoint_type remote_address,
        ConstBufferSequence const& buffers, socket_type&& socket)
    : BaseHTTPPeer(port, handler, socket.get_io_service(), journal, remote_address, buffers)
    , stream_(std::move(socket))
{
}

void
PlainHTTPPeer::run ()
{
    handler_.onAccept (session());
    if (! stream_.is_open())
        return;

    boost::asio::spawn (strand_, std::bind (&PlainHTTPPeer::do_read,
        shared_from_this(), std::placeholders::_1));
}

void
PlainHTTPPeer::do_request()
{
    ++request_count_;
    auto const what = handler_.onHandoff (session(),
        std::move(stream_), std::move(message_), remote_address_);
    if (what.moved)
        return;
    error_code ec;
    if (what.response)
    {
        // half-close on Connection: close
        if (! what.keep_alive)
            stream_.shutdown (socket_type::shutdown_receive, ec);
        if (ec)
            return fail (ec, "request");
        return write(what.response, what.keep_alive);
    }

    // Perform half-close when Connection: close and not SSL
    if (! message_.keep_alive())
        stream_.shutdown (socket_type::shutdown_receive, ec);
    if (ec)
        return fail (ec, "request");
    // legacy
    handler_.onRequest (session());
}

void
PlainHTTPPeer::do_close()
{
    error_code ec;
    stream_.shutdown (socket_type::shutdown_send, ec);
}

} // ripple

#endif
