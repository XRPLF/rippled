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

#ifndef BEAST_WSPROTO_SOCKET_H_INCLUDED
#define BEAST_WSPROTO_SOCKET_H_INCLUDED

#include <beast/wsproto/detail/socket_base.h>
#include <beast/asio/async_types.h>
#include <beast/asio/buffered_readstream.h>
#include <boost/asio.hpp>
#include <memory>

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
    void f(beast::http::message&);
    ws.set_option(decorator(f));
    @endcode

    Objects of this type are passed to stream::set_option.
*/
template<class... Args>
inline
auto
decorator(Args&&... args)
{
    return detail::decorator_type{
        std::forward<Args>(args)...};
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
    request. The default setting is to close connections after a
    failed upgrade request.

    This setting only affects the behavior of HTTP requests that
    implicitly or explicitly ask for a keepalive. For HTTP requests
    that indicate the connection should be closed, the connection is
    closed as per rfc2616.

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
*/
struct read_buffer
{
    std::size_t value;

    read_buffer(std::size_t n)
        : value(n)
    {
    }
};

//--------------------------------------------------------------------

/** WebSocket message metadata.
*/
struct msg_info
{
    /// Indicates the type of message (binary or text).
    opcode::value op;

    /// `true` if all octets for the current message are received.
    bool fin;
};

//--------------------------------------------------------------------

/// Provides message-oriented functionality using WebSocket.
/**
    The socket class template provides asynchronous and blocking
    message-oriented functionality necessary for clients and servers
    to utilize the WebSocket protocol.

    @par Thread Safety
    @e Distinct @e objects: Safe.@n
    @e Shared @e objects: Unsafe. The application must
    also ensure that all asynchronous operations are
    performed within the same implicit or explicit strand.

    @par Example
    To use the WebSocket socket template with an ip::tcp::socket,
    you would write:
    @code
    wsproto::socket<ip::tcp::socket> ws(io_service);
    @endcode
    Alternatively, you can write:
    @code
    ip::tcp::socket sock(io_service);
    wsproto::socket<ip::tcp::socket&> ws(sock);
    @endcode

    @par Concepts: AsyncReadStream, AsyncWriteStream,
    Decorator, Streambuf, SyncReadStream, SyncWriteStream.
*/
template<class Stream>
class socket : protected detail::socket_base
{
public:
    /// The type of the next layer.
    using next_layer_type =
        std::remove_reference_t<Stream>;

    /// The type of the lowest layer.
    using lowest_layer_type =
        typename next_layer_type::lowest_layer_type;

    /// The type of endpoint of the lowest layer.
    using endpoint_type =
        typename lowest_layer_type::endpoint_type;

    /// The protocol of the next layer.
    using protocol_type =
        typename lowest_layer_type::protocol_type;

    /// The type of resolver of the next layer.
    using resolver_type =
        typename protocol_type::resolver;

private:
    Stream next_layer_;
    asio::buffered_readstream<
        next_layer_type&, beast::asio::streambuf> stream_;

public:
    socket(socket&&) = default;
    socket(socket const&) = delete;
    socket& operator=(socket&&) = delete;
    socket& operator=(socket const&) = delete;

    /** Construct a websocket.

        This constructor creates a websocket and initialises the
        underlying stream object.

        @throws Any exceptions thrown by the Stream constructor.

        @param args The arguments to be passed to initialise the
        underlying stream object. The arguments are forwarded
        to the stream's constructor.
    */
    template<class... Args>
    explicit
    socket(Args&&... args);

    /** Destructor.
    */
    ~socket() = default;

    /** Get the io_service associated with the scket.

        This function may be used to obtain the io_service object
        that the socket uses to dispatch handlers for asynchronous
        operations.
    
        @return A reference to the io_service object that the socket
        will use to dispatch handlers. Ownership is not transferred
        to the caller.
    */
    boost::asio::io_service&
    get_io_service()
    {
        return next_layer_.lowest_layer().get_io_service();
    }

    /** Get a reference to the next layer.

        This function returns a reference to the next layer
        in a stack of stream layers.

        @return A reference to the next layer in the stack of
        stream layers. Ownership is not transferred to the caller.
    */
    next_layer_type&
    next_layer()
    {
        return next_layer_;
    }

    /** Get a reference to the next layer.

        This function returns a reference to the next layer in a
        stack of stream layers.

        @return A reference to the next layer in the stack of
        stream layers. Ownership is not transferred to the caller.
    */
    next_layer_type const&
    next_layer() const
    {
        return next_layer_;
    }

    /** Get a reference to the lowest layer.

        This function returns a reference to the lowest layer
        in a stack of stream layers.

        @return A reference to the lowest layer in the stack of
        stream layers. Ownership is not transferred to the caller.
    */
    lowest_layer_type&
    lowest_layer()
    {
        return next_layer_.lowest_layer();
    }

    /** Get a reference to the lowest layer.

        This function returns a reference to the lowest layer
        in a stack of stream layers.

        @return A reference to the lowest layer in the stack of
        stream layers. Ownership is not transferred to the caller.
    */
    lowest_layer_type const&
    lowest_layer() const
    {
        return next_layer_.lowest_layer();
    }

    /** Set options on the socket.

        The application must ensure that calls to set options
        are performed within the same implicit or explicit strand.

        @param args One or more socket options to set.
    */
    template<class A1, class A2, class... An>
    void
    set_option(A1&& a1, A2&& a2, An&&... an)
    {
        set_option(a1);
        set_option(a2, an...);
    }

    void
    set_option(detail::decorator_type o)
    {
        decorate_ = std::move(o);
    }

    void
    set_option(frag_size const& o)
    {
        wr_frag_ = o.value;
    }

    void
    set_option(keep_alive const& o)
    {
        keep_alive_ = o.value;
    }

    void
    set_option(read_buffer const& o)
    {
        stream_.reserve(o.value);
    }

    /// Read and respond to a WebSocket HTTP Upgrade request.
    /**
        TBD
    */
    void
    accept();

    /// Read and respond to a WebSocket HTTP Upgrade request.
    /**
        TBD
    */
    void
    accept(error_code& ec);

    /// Read and respond to a WebSocket HTTP Upgrade request.
    /**
        TBD
    */
    template<class ConstBufferSequence>
    void
    accept(ConstBufferSequence const& buffers);

    /// Read and respond to a WebSocket HTTP Upgrade request.
    /**
        TBD
    */
    template<class ConstBufferSequence>
    void
    accept(ConstBufferSequence const& buffers, error_code& ec);

    /// Respond to a WebSocket HTTP Upgrade request
    /*
        This function is used to synchronously send the HTTP response
        to a WebSocket HTTP Upgrade request.
        
        If the contents of the request are valid, the HTTP response
        indicates a successful upgrade and the socket is then ready
        to send and receive WebSocket protocol frames and messages.

        If the WebSocket HTTP Upgrade request cannot be satisfied,
        a HTTP response is sent indicating the reason and status
        code (typically 400, "Bad Request").

        The call blocks until one of the following conditions is true:

        @li An error occurs on the socket.

        @li The entire HTTP response has been sent.

        @throws boost::system::system_error Thrown on failure.

        @param m An object containing the HTTP Upgrade request.
    */
    void
    accept(beast::http::message const& m);

    /// Respond to a WebSocket HTTP Upgrade request
    /*
        This function is used to synchronously send the HTTP response
        to a WebSocket HTTP Upgrade request. If the contents of the
        request are valid, the HTTP response indicates a successful
        upgrade and the socket is then ready to send and receive
        WebSocket protocol frames and messages.

        If the WebSocket HTTP Upgrade request cannot be satisfied,
        a HTTP response is sent indicating the reason and status
        code (typically 400, "Bad Request").

        The call blocks until one of the following conditions is true:

        @li An error occurs on the socket.

        @li The entire HTTP response has been sent.

        @param m An object containing the HTTP Upgrade request.

        @param ec Set to indicate what error occurred, if any. If the
        upgrade request was refused, the error is set to <TBD>.
    */
    void
    accept(beast::http::message const& m, error_code& ec);

    /// Asynchronously read and respond to a WebSocket HTTP Upgrade request.
    /**
        TODO
    */
    template<class AcceptHandler>
    void
    async_accept(AcceptHandler&& handler);

    /// Asynchronously read and respond to a WebSocket HTTP Upgrade request.
    /**
        TODO
    */
    template<class ConstBufferSequence, class AcceptHandler>
    void
    async_accept(ConstBufferSequence const& buffers,
        AcceptHandler&& handler);

    /// Asynchronously responsd to a WebSocket HTTP Upgrade request.
    /*
        This function is used to asynchronously send the HTTP response
        to a WebSocket HTTP Upgrade request. This function call always
        returns immediately.

        If the contents of the request are valid, the HTTP response
        indicates a successful upgrade and the socket is then ready
        to send and receive WebSocket protocol frames and messages.

        If the WebSocket HTTP Upgrade request cannot be satisfied,
        a HTTP response is sent indicating the reason and status
        code (typically 400, "Bad Request").

        @param m An object containing the HTTP Upgrade request.

        @param h The handler to be called when the request completes.
        Copies will be made of the handler as required. The equivalent
        function signature of the handler must be:
        @code void handler(
            error_code const& error // result of operation
        ); @endcode
    */
    template<class AcceptHandler>
    void
    async_accept_request(beast::http::message const& m,
        AcceptHandler&& h);

    /// Send a WebSocket Upgrade request.
    /**
        This function is used to synchronously send the WebSocket
        upgrade HTTP request. The call blocks until one of the
        following conditions is true:

        @li An error occurs on the socket

        @li A complete HTTP response with the result of the upgrade
        request is received.

        @throws boost::system::system_error Thrown on failure.

        @param host The name of the remote host, required by
        the HTTP protocol.

        @param resource The requesting URI, which may not be empty,
        required by the HTTP protocol.

        @par Example
        @code
        wsproto::socket<boost::asio::ip::tcp::socket> ws(io_service);
        ...
        try
        {
            ws.upgrade("localhost", "/");
        }
        catch(...)
        {
            // An error occurred.
        }
        @endcode
    */
    void
    handshake(std::string const& host,
        std::string const& resource)
    {
        error_code ec;
        handshake(host, resource, ec);
        detail::maybe_throw(ec, "upgrade");
    }

    /// Send a WebSocket Upgrade request.
    /*
        This function is used to synchronously send the WebSocket
        upgrade HTTP request. The call blocks until one of the
        following conditions is true:

        @li An error occurs on the socket

        @li A complete HTTP response with the result of the upgrade
        request is received.
        
        @param host The name of the remote host, required by
        the HTTP protocol.

        @param resource The requesting URI, which may not be empty,
        required by the HTTP protocol.

        @param ec Set to indicate what error occurred, if any. If
        the remote host refuses the upgrade request, then ec is set
        to <TBD>.
        
        @par Example
        @code
        wsproto::socket<boost::asio::ip::tcp::socket> ws(io_service);
        ...
        error_code ec;
        ws.upgrade(host, resource, ec);
        if(ec)
        {
            // An error occurred.
        }
        @endcode
    */
    void
    handshake(std::string const& host,
        std::string const& resource, error_code& ec);

    /// Asynchronously send a WebSocket Upgrade request.
    /*
        This function is used to asynchronously send the WebSocket
        upgrade HTTP request. This function call always returns
        immediately.

        @param host The name of the remote host, required by
        the HTTP protocol. Copies may be made as needed.

        @param resource The requesting URI, which may not be empty,
        required by the HTTP protocol. Copies may be made as
        needed.

        @param h The handler to be called when the request completes.
        Copies will be made of the handler as required. The equivalent
        function signature of the handler must be:
        @code void handler(
            error_code const& error // result of operation
        ); @endcode
    */
    template<class HandshakeHandler>
    void
    async_handshake(std::string const& host,
        std::string const& resource, HandshakeHandler&& h);

    /// Perform a WebSocket close.
    /**
        This function initiates the WebSocket close procedure.
    */
    void
    close(std::uint16_t code, std::string const& reason)
    {
        error_code ec;
        close(code, reason, ec);
        detail::maybe_throw(ec, "close");
    }

    /// Perform a WebSocket close.
    /**
        This function initiates the WebSocket close procedure.
    */
    void
    close(std::uint16_t code,
        std::string const& reason, error_code& ec);

    /// Asynchronously perform a WebSocket close.
    /**
        This function initiates or completes the WebSocket
        close procedure.
    */
    template<class CloseHandler>
    void
    async_close(CloseHandler&& handler);

    /// Asynchronously perform a WebSocket close.
    /**
        This function initiates the WebSocket close procedure.
    */
    template<class CloseHandler>
    void
    async_close(std::uint16_t code,
        std::string const& reason, CloseHandler&& handler);

    /** Read some message data.

        This function is used to read message data from the websocket.
        The function call will block until one or more bytes of data
        has been read successfully, the end of the current message is
        reached, or an error occurs.

        On success, mi is filled out to reflect the message payload
        contents. op is set to binary or text, and the fin flag
        indicates if all the message data has been read in. To read the
        entire message, callers should repeat the read_some operation
        until mi.fin is true. A message with no payload will have
        mi.fin == true, and zero bytes placed into the stream buffer.

        @param mi An object to store metadata about the message.
        This object must remain valid until the handler is called.

        @param streambuf A stream buffer to hold the message data.
        This object must remain valid until the handler is called.

        @throws boost::system::system_error Thrown on failure.

        @see read
    */
    template<class Streambuf>
    void
    read_some(msg_info& mi, Streambuf& streambuf)
    {
        error_code ec;
        read_some(mi, streambuf, ec);
        detail::maybe_throw(ec, "read_some");
    }

    /** Read some message data.

        This function is used to read message data from the websocket.
        The function call will block until one or more bytes of data
        has been read successfully, the end of the current message is
        reached, or an error occurs.

        On success, mi is filled out to reflect the message payload
        contents. op is set to binary or text, and the fin flag
        indicates if all the message data has been read in. To read the
        entire message, callers should repeat the read_some operation
        until mi.fin is true. A message with no payload will have
        mi.fin == true, and zero bytes placed into the stream buffer.

        @param mi An object to store metadata about the message.
        This object must remain valid until the handler is called.

        @param streambuf A stream buffer to hold the message data.
        This object must remain valid until the handler is called.

        @param ec Set to indicate what error occurred, if any.

        @see read
    */
    template<class Streambuf>
    void
    read_some(msg_info& mi, 
        Streambuf& msg_info, error_code& ec);

    /** Start reading message data asynchronously.

        This function is used to asychronously read message data from
        the websocket. The function call always returns immediately.

        Upon a successful completion, mi is filled out to reflect
        the message payload contents. op is set to binary or text, and
        the fin flag indicates if all the message data has been read in.
        To read the entire message, callers should repeat the
        async_read_some operation until mi.fin is true. A message with
        no payload will have mi.fin == true, and zero bytes placed into
        the stream buffer.

        @param mi An object to store metadata about the message.
        This object must remain valid until the handler is called.

        @param streambuf A stream buffer to hold the message data.
        This object must remain valid until the handler is called.

        @param handler The handler to be called when the read operation
        completes. Copies will be made of the handler as required. The
        function signature of the handler must be:
        @code
        void handler(
            error_code const& error     // Result of operation
        );
        @endcode
        Regardless of whether the asynchronous operation completes
        immediately or not, the handler will not be invoked from within
        this function. Invocation of the handler will be performed in a
        manner equivalent to using boost::asio::io_service::post().

        @see async_read
    */
    template<class Streambuf, class ReadHandler>
    void
    async_read_some(msg_info& mi,
        Streambuf& streambuf, ReadHandler&& handler);

    /// Write an entire frame to a stream before returning.
    /**
        This function is used to write a frame to a stream. The
        call will block until one of the following conditions is true:

        @li All of the data in the supplied buffers has been written.

        @li An error occurs.

        This operation is implemented in terms of one or more calls
        to the stream's write_some function. The actual payload sent
        may be transformed as per the WebSocket protocol settings.

        @param op The opcode, which must be text or binary.

        @param fin `true` if this is the last frame in the message.

        @param buffers One or more buffers containing the frame's
        payload data.

        @param ec Set to indicate what error occurred, if any.
    */
    template<class ConstBufferSequence>
    void
    write(opcode::value op, bool fin,
        ConstBufferSequence const& buffers, error_code& ec);

    /// Write an entire frame to a stream before returning.
    /**
        This function is used to write a frame to a stream. The
        call will block until one of the following conditions is true:

        @li All of the data in the supplied buffers has been written.

        @li An error occurs.

        This operation is implemented in terms of one or more calls
        to the stream's write_some function. The actual payload sent
        may be transformed as per the WebSocket protocol settings.

        @throws boost::system::system_error Thrown on failure.

        @param fin `true` if this is the last frame in the message.

        @param buffers One or more buffers containing the frame's
        payload data.
    */
    template<class ConstBufferSequence>
    void
    write(opcode::value op, bool fin,
        ConstBufferSequence const& buffers)
    {
        error_code ec;
        write(op, fin, buffers, ec);
        detail::maybe_throw(ec, "write");
    }

    /// Start writing a frame asynchronously
    /**
        This function is used to asynchronously write a WebSocket
        frame on the stream. This function call always returns
        immediately.

        @param op The opcode, which must be text or binary.

        @param fin A bool indicating whether or not the frame is the
        last frame in the corresponding WebSockets message.

        @param buffers A object meeting the requirements of
        ConstBufferSequence which holds the payload data before any
        masking or compression. Although the buffers object may be copied
        as necessary, ownership of the underlying buffers is retained by
        the caller, which must guarantee that they remain valid until
        the handler is called. 

        @param handler The handler to be called when the write completes.
        Copies will be made of the handler as required. The equivalent
        function signature of the handler must be:
        @code void handler(
            boost::system::error_code const& error // result of operation
        ); @endcode
    */
    template<class ConstBufferSequence, class WriteHandler>
    void
    async_write(opcode::value op, bool fin,
        ConstBufferSequence const& buffers,
            WriteHandler&& handler);

private:
    template<class Handler> class accept_op;
    template<class Handler> class close_op;
    template<class Handler> class handshake_op;
    template<class Streambuf, class Handler> class read_some_op;
    template<class Buffers, class Handler> class write_op;

    template<class Streambuf>
    void
    write_error(Streambuf& sb, error_code const& ec);

    template<class Streambuf>
    void
    write_response(Streambuf& sb,
        beast::http::message const& req);

    beast::http::message
    make_upgrade(std::string const& host,
        std::string const& resource);

    beast::asio::streambuf
    make_response(beast::http::message const& r);

    error_code
    do_accept(beast::http::message const& req);

    void
    do_response(beast::http::message const& response,
        error_code& ec);

    void
    do_fail(close::value code, error_code& ec);
};

//------------------------------------------------------------------------------

/** Read a message.

    This function is used to read a message from the websocket.
    The function call will block until the message has been
    read successfully, or until an error occurs.

    On success op is set to reflect the message type, binary
    or text.

    @param ws The websocket to read from.

    @param op A value to receive the message type.
    This object must remain valid until the handler is called.

    @param streambuf A stream buffer to hold the message data.
    This object must remain valid until the handler is called.

    @throws boost::system::system_error Thrown on failure.
*/
template<class Stream, class Streambuf>
void
read(socket<Stream>& ws, opcode::value& op,
    Streambuf& streambuf)
{
    error_code ec;
    read(ws, op, streambuf, ec);
    detail::maybe_throw(ec, "read");
}

/** Read a message.

    This function is used to read a message from the websocket.
    The function call will block until the message has been
    read successfully, or until an error occurs.

    On success op is set to reflect the message type, binary
    or text.

    @param ws The websocket to read from.

    @param op A value to receive the message type.
    This object must remain valid until the handler is called.

    @param streambuf A stream buffer to hold the message data.
    This object must remain valid until the handler is called.

    @param ec Set to indicate what error occurred, if any.
*/
template<class Stream, class Streambuf>
void
read(socket<Stream>& ws, opcode::value& op,
    Streambuf& streambuf, error_code& ec);

/** Start reading a message asynchronously.

    This function is used to asychronously read a message from
    the websocket. The function call always returns immediately.

    Upon a successful completion, op is set to either binary or
    text depending on the message type, and the input area of the
    streambuf will hold all the message payload bytes (which may
    be zero in length).

    @param ws The websocket to read from.

    @param op A value to receive the message type.
    This object must remain valid until the handler is called.

    @param streambuf A stream buffer to hold the message data.
    This object must remain valid until the handler is called.

    @param handler The handler to be called when the read operation
    completes. Copies will be made of the handler as required. The
    function signature of the handler must be:
    @code
    void handler(
        error_code const& error     // Result of operation
    );
    @endcode
    Regardless of whether the asynchronous operation completes
    immediately or not, the handler will not be invoked from within
    this function. Invocation of the handler will be performed in a
    manner equivalent to using boost::asio::io_service::post().
*/
template<class Stream, class Streambuf, class ReadHandler>
BOOST_ASIO_INITFN_RESULT_TYPE(ReadHandler,
    void(error_code))
async_read(socket<Stream>& ws, opcode::value& op,
    Streambuf& streambuf, ReadHandler&& handler);

/// Write a complete WebSocket message.
/*
*/
template<class Stream, class ConstBufferSequence>
void
write_msg(socket<Stream>& ws, opcode::value op,
    ConstBufferSequence const& buffers)
{
    error_code ec;
    write_msg(ws, op, buffers, ec);
    detail::maybe_throw(ec, "write_msg");
}

/// Write a complete WebSocket message.
/*
*/
template<class Stream, class ConstBufferSequence>
void
write_msg(socket<Stream>& ws, opcode::value op,
    ConstBufferSequence const& buffers, error_code& ec);

template<class Stream,
    class ConstBufferSequence, class WriteHandler>
void
async_write_msg(socket<Stream>& ws, opcode::value op,
    ConstBufferSequence const& buffers, WriteHandler&& handler);

} // wsproto
} // beast

#include <beast/wsproto/impl/socket.ipp>

#endif
