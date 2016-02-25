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

#ifndef BEAST_WSPROTO_OPTION_H_INCLUDED
#define BEAST_WSPROTO_OPTION_H_INCLUDED

#include <beast/wsproto/detail/socket_base.h>
#include <cstdint>
#include <type_traits>

namespace beast {
namespace wsproto {

/** HTTP decorator option.

    The decorator transforms the HTTP requests and responses used
    when requesting or responding to the WebSocket Upgrade. This may
    be used to set or change header fields. For example to set the
    Server or User-Agent fields. The default setting applies no
    transformation to the HTTP message.

    For synchronous operations, the implementation will call the
    decorator before the function call to perform the operation
    returns.

    For asynchronous operations, the implementation guarantees that
    calls to the decorator will be made from the same implicit or
    explicit strand used to call the asynchronous initiation
    function.

    Usage:
    @code
    struct identity
    {
        template<bool isRequest, class Body, class Allocator>
        void
        operator()(http::message<isRequest, Body, Allocator>& m)
        {
            if(isRequest)
                m.headers.replace("User-Agent", "MyClient");
            else
                m.headers.replace("Server", "MyServer");
        }
    };
    ws.set_option(decorator(identity{}));
    @endcode

    The default setting is no decorator.

    Objects of this type are passed to stream::set_option.
*/
template<class Decorator>
inline
auto
decorate(Decorator&& d)
{
    return std::make_unique<detail::decorator<
        std::decay_t<Decorator>>>(
            std::forward<Decorator>(d));
}

/** Outgoing message fragment size option.

    Sets the maximum size of fragments generated when sending
    messages on a WebSocket socket.

    The default setting is to not automatically fragment frames.

    Objects of this type are passed to stream::set_option.
*/
struct frag_size
{
    std::size_t value;

    frag_size(std::size_t n)
        : value(n)
    {
    }
};

/** Keep-alive option.

    Determines if the connection is closed after a failed upgrade
    request.

    This setting only affects the behavior of HTTP requests that
    implicitly or explicitly ask for a keepalive. For HTTP requests
    that indicate the connection should be closed, the connection is
    closed as per rfc2616.

    The default setting is to close connections after a failed
    upgrade request.

    Objects of this type are passed to stream::set_option.
*/
struct keep_alive
{
    bool value;

    keep_alive(bool v)
        : value(v)
    {
    }
};

/** Read buffer size option.

    Sets the number of bytes allocated to the socket's read buffer.
    If this is zero, then reads are not buffered. Setting this
    higher can improve performance when many small frames are
    received.

    The default is no buffering.

    Objects of this type are passed to stream::set_option.
*/
struct read_buffer
{
    std::size_t value;

    read_buffer(std::size_t n)
        : value(n)
    {
    }
};

} // wsproto
} // beast

#endif
