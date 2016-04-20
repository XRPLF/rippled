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

#ifndef BEAST_ASIO_SSL_BUNDLE_H_INCLUDED
#define BEAST_ASIO_SSL_BUNDLE_H_INCLUDED

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <memory>
#include <utility>

namespace beast {
namespace asio {

/** Work-around for non-movable boost::ssl::stream.
    This allows ssl::stream to be movable and allows the stream to
    construct from an already-existing socket.
*/
struct ssl_bundle
{
    using socket_type = boost::asio::ip::tcp::socket;
    using stream_type = boost::asio::ssl::stream <socket_type&>;
    using shared_context = std::shared_ptr<boost::asio::ssl::context>;

    template <class... Args>
    ssl_bundle (shared_context const& context_, Args&&... args);

    // DEPRECATED
    template <class... Args>
    ssl_bundle (boost::asio::ssl::context& context_, Args&&... args);

    shared_context context;
    socket_type socket;
    stream_type stream;
};

template <class... Args>
ssl_bundle::ssl_bundle (shared_context const& context_, Args&&... args)
    : socket(std::forward<Args>(args)...)
    , stream (socket, *context_)
{
}

template <class... Args>
ssl_bundle::ssl_bundle (boost::asio::ssl::context& context_, Args&&... args)
    : socket(std::forward<Args>(args)...)
    , stream (socket, context_)
{
}

} // asio
} // beast

#endif
