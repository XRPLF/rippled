//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_OPTION_HPP
#define BEAST_WEBSOCKET_OPTION_HPP

#include <beast/websocket/detail/stream_base.hpp>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace beast {
namespace websocket {

/** Automatic fragmentation size option.

    Sets the maximum size of fragments generated when sending messages
    on a WebSocket stream.

    When the automatic fragmentation size is non-zero, messages exceeding
    the size will be split into multiple frames no larger than the size.
    This setting does not affect frames sent explicitly using
    @ref stream::write_frame or @ref stream::async_write_frame.

    The default setting is to fragment messages into 16KB frames.

    @note Objects of this type are passed to @ref stream::set_option.

    @par Example
    Setting the automatic fragmentation size option:
    @code
    ...
    websocket::stream<ip::tcp::socket> stream(ios);
    stream.set_option(auto_fragment_size{8192});
    @endcode
*/
#if GENERATING_DOCS
using auto_fragment_size = implementation_defined;
#else
struct auto_fragment_size
{
    std::size_t value;

    auto_fragment_size(std::size_t n)
        : value(n)
    {
    }
};
#endif

/** HTTP decorator option.

    The decorator transforms the HTTP requests and responses used
    when requesting or responding to the WebSocket Upgrade. This may
    be used to set or change header fields. For example to set the
    Server or User-Agent fields. The default setting applies no
    transformation to the HTTP message.

    The context in which the decorator is called depends on the
    type of operation performed:

    @li For synchronous operations, the implementation will call the
    decorator before the operation unblocks.

    @li For asynchronous operations, the implementation guarantees
    that calls to the decorator will be made from the same implicit
    or explicit strand used to call the asynchronous initiation
    function.

    The default setting is no decorator.

    @note Objects of this type are passed to @ref stream::set_option.

    @par Example
    Setting the decorator.
    @code
    struct identity
    {
        template<bool isRequest, class Body, class Headers>
        void
        operator()(http::message<isRequest, Body, Headers>& m)
        {
            if(isRequest)
                m.headers.replace("User-Agent", "MyClient");
            else
                m.headers.replace("Server", "MyServer");
        }
    };
    ...
    websocket::stream<ip::tcp::socket> ws(ios);
    ws.set_option(decorate(identity{}));
    @endcode
*/
#if GENERATING_DOCS
using decorate = implementation_defined;
#else
template<class Decorator>
inline
detail::decorator_type
decorate(Decorator&& d)
{
    return detail::decorator_type{new
        detail::decorator<typename std::decay<Decorator>::type>{
            std::forward<Decorator>(d)}};
}
#endif

/** Keep-alive option.

    Determines if the connection is closed after a failed upgrade
    request.

    This setting only affects the behavior of HTTP requests that
    implicitly or explicitly ask for a keepalive. For HTTP requests
    that indicate the connection should be closed, the connection is
    closed as per rfc2616.

    The default setting is to close connections after a failed
    upgrade request.

    @note Objects of this type are passed to @ref stream::set_option.

    @par Example
    Setting the keep alive option.
    @code
    ...
    websocket::stream<ip::tcp::socket> ws(ios);
    ws.set_option(keep_alive{8192});
    @endcode
*/
#if GENERATING_DOCS
using keep_alive = implementation_defined;
#else
struct keep_alive
{
    bool value;

    keep_alive(bool v)
        : value(v)
    {
    }
};
#endif

/** Message type option.

    This controls the opcode set for outgoing messages. Valid
    choices are opcode::binary or opcode::text. The setting is
    only applied at the start when a caller begins a new message.
    Changing the opcode after a message is started will only
    take effect after the current message being sent is complete.

    The default setting is opcode::text.

    @note Objects of this type are passed to @ref stream::set_option.

    @par Example
    Setting the message type to binary.
    @code
    ...
    websocket::stream<ip::tcp::socket> ws(ios);
    ws.set_option(message_type{opcode::binary});
    @endcode
*/
#if GENERATING_DOCS
using message_type = implementation_defined;
#else
struct message_type
{
    opcode value;

    explicit
    message_type(opcode op)
    {
        if(op != opcode::binary && op != opcode::text)
            throw std::domain_error("bad opcode");
        value = op;
    }
};
#endif

/** Read buffer size option.

    Sets the number of bytes allocated to the socket's read buffer.
    If this is zero, then reads are not buffered. Setting this
    higher can improve performance when expecting to receive
    many small frames.

    The default is no buffering.

    @note Objects of this type are passed to @ref stream::set_option.

    @par Example
    Setting the read buffer size.
    @code
    ...
    websocket::stream<ip::tcp::socket> ws(ios);
    ws.set_option(read_buffer_size{16 * 1024});
    @endcode
*/
#if GENERATING_DOCS
using read_buffer_size = implementation_defined;
#else
struct read_buffer_size
{
    std::size_t value;

    explicit
    read_buffer_size(std::size_t n)
        : value(n)
    {
    }
};
#endif

/** Maximum incoming message size option.

    Sets the largest permissible incoming message size. Message
    frame headers indicating a size that would bring the total
    message size over this limit will cause a protocol failure.

    The default setting is 16 megabytes.

    @note Objects of this type are passed to @ref stream::set_option.

    @par Example
    Setting the maximum read message size.
    @code
    ...
    websocket::stream<ip::tcp::socket> ws(ios);
    ws.set_option(read_message_max{65536});
    @endcode
*/
#if GENERATING_DOCS
using read_message_max = implementation_defined;
#else
struct read_message_max
{
    std::size_t value;

    explicit
    read_message_max(std::size_t n)
        : value(n)
    {
    }
};
#endif

/** Write buffer size option.

    Sets the number of bytes allocated to the socket's write buffer.
    This buffer is used to hold masked frame payload data. Lowering
    the size of the buffer can decrease the memory requirements for
    each connection, at the cost of an increased number of calls to
    perform socket writes.

    This setting does not affect connections operating in the server
    role, since servers do not apply a masking key to frame payloads.

    The default setting is 4096. The minimum value is 1024.

    @note Objects of this type are passed to @ref stream::set_option.

    @par Example
    Setting the write buffer size.
    @code
    ...
    websocket::stream<ip::tcp::socket> ws(ios);
    ws.set_option(write_buffer_size{8192});
    @endcode
*/
#if GENERATING_DOCS
using write_buffer_size = implementation_defined;
#else
struct write_buffer_size
{
    std::size_t value;

    explicit
    write_buffer_size(std::size_t n)
        : value(n)
    {
    }
};
#endif

} // websocket
} // beast

#endif
