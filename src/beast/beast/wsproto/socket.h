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

#include <beast/wsproto/http.h>
#include <beast/wsproto/detail/socket_base.h>
#include <beast/http/parser.h>
#include <boost/asio.hpp>
#include <memory>
#include <type_traits>

namespace beast {
namespace wsproto {

/// Keep-alive option.
/**
    Determines if the connection is closed after a failed
    upgrade request. 

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

/// Message fragment size option.
/**
    Sets the maximum size of fragments generated when sending
    messages on a WebSocket socket.

    The default setting is to not automatically fragment frames.

    Objects of this type are passed to stream::set_option.
*/
struct frag_size
{
    std::size_t value;

    /// Set the fragment size.
    /**
        @param n The maximum number of bytes per fragment. If
        this is zero, then messages are not fragmented.
    */
    frag_size(std::size_t n)
        : value(n)
    {
    }
};

//--------------------------------------------------------------------

/// Provides message-oriented functionality using WebSocket.
/**
    The socket class template provides asynchronous and blocking
    message-oriented and frame-oriented functionality necessary for
    clients and servers to utilize the WebSocket protocol.

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

    static_assert(
        std::is_class<next_layer_type>::value &&
        ! std::is_const<next_layer_type>::value &&
        ! std::is_volatile<next_layer_type>::value,
            "Stream does not meet the type requirements");

private:
    Stream stream_;

public:
    socket(socket&&) = default;
    socket(socket const&) = delete;
    socket& operator= (socket&&) = delete;
    socket& operator= (socket const&) = delete;

    /// Construct a socket.
    /**
        This constructor creates a socket and initialises the
        underlying stream object.

        @throws Any exceptions thrown by the Stream constructor.

        @param args The arguments to be passed to initialise the
        underlying stream object. The arguments are forwarded
        to the stream's constructor.
    */
    template<class... Args>
    explicit
    socket(Args&&... args);

    /// Destructor.
    ~socket() = default;

    /// Get the io_service associated with the object.
    /**
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
        return stream_.lowest_layer().get_io_service();
    }

    /// Get a reference to the next layer.
    /**
        This function returns a reference to the next layer
        in a stack of stream layers.

        @return A reference to the next layer in the stack of
        stream layers. Ownership is not transferred to the caller.
    */
    next_layer_type&
    next_layer()
    {
        return stream_;
    }

    /// Get a reference to the next layer.
    /**
        This function returns a reference to the next layer in a
        stack of stream layers.

        @return A reference to the next layer in the stack of
        stream layers. Ownership is not transferred to the caller.
    */
    next_layer_type const&
    next_layer() const
    {
        return stream_;
    }

    /// Get a reference to the lowest layer.
    /**
        This function returns a reference to the lowest layer
        in a stack of stream layers.

        @return A reference to the lowest layer in the stack of
        stream layers. Ownership is not transferred to the caller.
    */
    lowest_layer_type&
    lowest_layer()
    {
        return stream_.lowest_layer();
    }

    /// Get a reference to the lowest layer.
    /**
        This function returns a reference to the lowest layer
        in a stack of stream layers.

        @return A reference to the lowest layer in the stack of
        stream layers. Ownership is not transferred to the caller.
    */
    lowest_layer_type const&
    lowest_layer() const
    {
        return stream_.lowest_layer();
    }

    /// Set options on the socket.
    /**
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
    set_option(keep_alive const& o)
    {
        keep_alive_ = o.value;
    }

    void
    set_option(frag_size const& o)
    {
        wr_frag_ = o.value;
    }

    /// Set the HTTP message decorator on this object.
    /*
        The decorator is used to add custom fields to outbound
        HTTP messages. This could be used, for example, to set
        the Server or other fields.
    */
    template<class Decorator>
    void
    decorate(Decorator&& d);

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
    accept(error_code& ec)
    {
        accept(boost::asio::null_buffers{}, ec);
    }

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
    async_accept(AcceptHandler&& handler)
    {
        async_accept(boost::asio::null_buffers{},
            std::forward<AcceptHandler>(handler));
    }

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

    /// Read a frame header.
    /*
        This function is used to read a WebSocket frame header on
        the stream. The call will block until one of the following
        conditions is true:

        @li A text, binary, or continuation frame header is read.

        @li An error occurs on the stream.

        Any control frames encountered during the read operation will
        be processed transparently before the call returns.

        If the call completes successfully the next read on the stream
        will be for payload data.

        @throws boost::system::system_error Thrown on failure.

        @param fh An object to hold the frame header information.
        This reference must remain valid until the asynchronous
        operation is complete.
    */
    void
    read(frame_header& fh)
    {
        error_code ec;
        read(fh, ec);
        detail::maybe_throw(ec, "read");
    }

    /// Read a frame header.
    /*
        This function is used to read a WebSocket frame header on
        the stream. The call will block until one of the following
        conditions is true:

        @li A text, binary, or continuation frame header is read.

        @li An error occurs on the stream.

        Any control frames encountered during the read operation will
        be processed transparently before the call returns.

        If the call completes successfully the next read on the stream
        will be for payload data.

        @param fh An object to hold the frame header information.
        This reference must remain valid until the asynchronous
        operation is complete.

        @param ec Set to indicate what error occurred, if any.
    */
    void
    read(frame_header& fh, error_code& ec);

    /// Start reading a frame header asynchronously.
    /**
        This function is used to asynchronously read a WebSocket
        frame header on the stream. This function call always returns
        immediately.

        The completion handler will be called when one of the following
        conditions is true:

        @li A non-control frame header is completely read.

        @li An error occurred.

        Any control frames encountered during the read operation will
        be processed transparently before the call returns.

        If the call completes successfully the next read on the stream
        must be for payload data.

        @param fh An object to hold the frame header information.
        This reference must remain valid until the asynchronous
        operation is complete.

        @param handler The handler to be called when the read completes.
        Copies will be made of the handler as required. The equivalent
        function signature of the handler must be:
        @code void handler(
            error_code const& error // result of operation
        ); @endcode
    */
    template<class ReadHandler>
    void
    async_read(frame_header& fh, ReadHandler&& handler);

    /// Read payload data.
    /**
        This function is used to read the WebSocket payload data
        on the stream. The call will block until one of the following
        conditions is true:

        @li The supplied buffers are full. That is, the bytes
        transferred is equal to the sum of the buffer sizes.

        @li An error occurred.

        This should only be called after a successful call to read
        the frame header, and only if the frame header indicates a
        length greater than zero.

        If the call is successful and all the bytes in the payload
        have been read, the next read on the stream must be for a
        frame header.

        @param buffers One or more buffers into which the frame
        payload data will be read.

        @return The number of bytes placed into buffers.

        @throws boost::system::system_error Thrown on failure.
    */
    template<class MutableBufferSequence>
    std::size_t
    read(MutableBufferSequence const& buffers)
    {
        error_code ec;
        auto const n = read(buffers, ec);
        detail::maybe_throw(ec, "read_some");
        return n;
    }

    /// Read payload data.
    /**
        This function is used to read the WebSocket payload data
        on the stream. The call will block until one of the following
        conditions is true:

        @li The supplied buffers are full. That is, the bytes
        transferred is equal to the sum of the buffer sizes.

        @li There is no more payload data remaining.

        @li An error occurred.

        This should only be called after a successful call to read
        the frame header, and only if the frame header indicates a
        length greater than zero.

        If the call is successful and all the bytes in the payload
        have been read, the next read on the stream must be for a
        frame header.

        @param buffers One or more buffers into which the frame
        payload data will be read.

        @param ec Set to indicate what error occurred, if any.

        @return The number of bytes placed into buffers.
    */
    template<class MutableBufferSequence>
    std::size_t
    read(MutableBufferSequence const& buffers, error_code& ec);

    /// Start reading payload data asynchronously.
    /**
        This function is used to asynchronously read the WebSocket
        payload data on the stream. This function call always
        returns immediately.

        The completion handler will be called when one of the
        following conditions is met:

        @li The supplied buffers are full. That is, the bytes
        transferred is equal to the sum of the buffer sizes.

        @li There is no more payload data remaining.

        @li An error occurred.

        @param buffers One or more buffers into which the frame
        payload data will be read. Although the buffers object may be
        copied or moved as necessary, ownership of the underlying
        memory blocks is retained by the caller, which must guarantee
        that they remain valid until the handler is called.

        @param handler The handler to be called when the read
        completes. Copies will be made of the handler as required.
        The equivalent function signature of the handler must be:
        @code void handler(
            boost::system::error_code const& error, // result of operation
            std::size_t bytes_transferred // the number of bytes copied to buffers
        ); @endcode
        Regardless of whether the asynchronous operation completes
        immediately or not, the handler will not be invoked from
        within this function. Invocation of the handler will be
        performed in a manner equivalent to using
        boost::asio::io_service::post().
    */
    template<class MutableBufferSequence,
        class ReadPayloadHandler>
    void
    async_read(MutableBufferSequence const& buffers,
        ReadPayloadHandler&& handler);

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
    BOOST_ASIO_INITFN_RESULT_TYPE(WriteHandler,
        void(boost::system::error_code))
    async_write(opcode::value op, bool fin,
        ConstBufferSequence const& buffers,
            WriteHandler&& handler);

private:
    template<class Handler> class accept_op;
    template<class Handler> class close_op;
    template<class Handler> class handshake_op;
    template<class Handler> class header_op;

    template<class Buffers, class Handler> class read_op;
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
    do_close(close::value code, error_code& ec);
};

//------------------------------------------------------------------------------

/// Read a complete WebSocket message.
/*
    @return The number of bytes in the message.
    This will be zero for messages without a payload.
*/
template<class Stream, class Streambuf>
std::size_t
read_msg(socket<Stream>& ws,
    opcode::value& op, Streambuf& sb)
{
    error_code ec;
    read_msg(ws, op, sb, ec);
    detail::maybe_throw(ec, "read_msg");
}

/// Read a complete WebSocket message.
/*
    @return The number of bytes in the message.
    This will be zero for messages without a payload.
*/
template<class Stream, class Streambuf>
void
read_msg(socket<Stream>& ws,
    opcode::value& op, Streambuf& sb, error_code& ec);

/// Read a complete WebSocket message asynchronously.
/*
    This operation is implemented as one or more calls to the
    socket's async_read_some function.

    @param ws The WebSocket object.

    @param sb An object that meets the requirements of Streambuf.

    @param h The handler to be called when the read completes.
    Copies will be made of the handler as required. The equivalent
    function signature of the handler must be:
    @code void handler(
        boost::system::error_code const& error // result of operation
    ); @endcode
*/
template<class Stream, class Streambuf, class ReadHandler>
void
async_read_msg(socket<Stream>& ws, opcode::value& op,
    Streambuf& sb, ReadHandler&& handler);

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
