//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_STREAM_HPP
#define BEAST_WEBSOCKET_STREAM_HPP

#include <beast/websocket/option.hpp>
#include <beast/websocket/detail/stream_base.hpp>
#include <beast/http/message_v1.hpp>
#include <beast/http/string_body.hpp>
#include <beast/streambuf_readstream.hpp>
#include <beast/async_completion.hpp>
#include <beast/detail/get_lowest_layer.hpp>
#include <boost/asio.hpp>
#include <boost/utility/string_ref.hpp>
#include <algorithm>
#include <cstdint>
#include <limits>

namespace beast {
namespace websocket {

/** Information about a WebSocket frame.

    This information is provided to callers during frame
    read operations.
*/
struct frame_info
{
    /// Indicates the type of message (binary or text).
    opcode op;

    /// `true` if this is the last frame in the current message.
    bool fin;
};

//--------------------------------------------------------------------

/** Provides message-oriented functionality using WebSocket.

    The stream class template provides asynchronous and blocking
    message-oriented functionality necessary for clients and servers
    to utilize the WebSocket protocol.

    @par Thread Safety
    @e Distinct @e objects: Safe.@n
    @e Shared @e objects: Unsafe. The application must ensure that
    all asynchronous operations are performed within the same
    implicit or explicit strand.

    @par Example

    To use the WebSocket stream template with an
    ip::tcp::socket, you would write:

    @code
    websocket::stream<ip::tcp::socket> ws(io_service);
    @endcode
    Alternatively, you can write:
    @code
    ip::tcp::socket sock(io_service);
    websocket::stream<ip::tcp::socket&> ws(sock);
    @endcode

    @tparam NextLayer An object meeting the requirements of ReadStream,
    WriteStream, AsyncReadStream, and AsyncWriteStream.

    @note A stream object must not be destroyed while there are
    pending asynchronous operations associated with it.

    @par Concepts
    AsyncReadStream, AsyncWriteStream,
    Decorator, Streambuf, SyncReadStream, SyncWriteStream.
*/
template<class NextLayer>
class stream : public detail::stream_base
{
    friend class ws_test;

    streambuf_readstream<NextLayer, streambuf> stream_;

public:
    /// The type of the next layer.
    using next_layer_type =
        typename std::remove_reference<NextLayer>::type;

    /// The type of the lowest layer.
    using lowest_layer_type =
        typename beast::detail::get_lowest_layer<
            next_layer_type>::type;

    /** Move-construct a stream.

        If @c NextLayer is move constructible, this function
        will move-construct a new stream from the existing stream.

        @note The behavior of move assignment on or from streams
        with active or pending operations is undefined.
    */
    stream(stream&&) = default;

    /** Move assignment.

        If @c NextLayer is move constructible, this function
        will move-construct a new stream from the existing stream.

        @note The behavior of move assignment on or from streams
        with active or pending operations is undefined.
    */
    stream& operator=(stream&&) = default;

    /** Construct a WebSocket stream.

        This constructor creates a websocket stream and initialises
        the next layer object.

        @throws Any exceptions thrown by the NextLayer constructor.

        @param args The arguments to be passed to initialise the
        next layer object. The arguments are forwarded to the next
        layer's constructor.
    */
    template<class... Args>
    explicit
    stream(Args&&... args);

    /** Destructor.

        A stream object must not be destroyed while there are
        pending asynchronous operations associated with it.
    */
    ~stream() = default;

    /** Set options on the stream.

        The application must ensure that calls to set options
        are performed within the same implicit or explicit strand.

        @param args One or more stream options to set.
    */
#if GENERATING_DOCS
    template<class... Args>
    void
    set_option(Args&&... args)
#else
    template<class A1, class A2, class... An>
    void
    set_option(A1&& a1, A2&& a2, An&&... an)
#endif
    {
        set_option(std::forward<A1>(a1));
        set_option(std::forward<A2>(a2),
            std::forward<An>(an)...);
    }

    void
    set_option(auto_fragment_size const& o)
    {
        if(o.value <= 0)
            wr_frag_size_ =
                std::numeric_limits<std::size_t>::max();
        else
            wr_frag_size_ = o.value;
    }

    void
    set_option(detail::decorator_type o)
    {
        d_ = std::move(o);
    }

    void
    set_option(keep_alive const& o)
    {
        keep_alive_ = o.value;
    }

    void
    set_option(message_type const& o)
    {
        wr_opcode_ = o.value;
    }

    void
    set_option(read_buffer_size const& o)
    {
        stream_.reserve(o.value);
    }

    void
    set_option(read_message_max const& o)
    {
        rd_msg_max_ = o.value;
    }

    void
    set_option(write_buffer_size const& o)
    {
        wr_buf_size_ = std::max<std::size_t>(o.value, 1024);
        stream_.reserve(o.value);
    }

    /** Get the io_service associated with the stream.

        This function may be used to obtain the io_service object
        that the stream uses to dispatch handlers for asynchronous
        operations.

        @return A reference to the io_service object that the stream
        will use to dispatch handlers. Ownership is not transferred
        to the caller.
    */
    boost::asio::io_service&
    get_io_service()
    {
        return stream_.get_io_service();
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
        return stream_.next_layer();
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
        return stream_.next_layer();
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
        return stream_.lowest_layer();
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
        return stream_.lowest_layer();
    }

    /** Returns the close reason received from the peer.

        This is only valid after a read completes with error::closed.
    */
    close_reason const&
    reason() const
    {
        return cr_;
    }

    /** Read and respond to a WebSocket HTTP Upgrade request.

        This function is used to synchronously read a HTTP WebSocket
        Upgrade request and send the HTTP response.

        If the contents of the request are valid, the HTTP response
        indicates a successful upgrade and the stream is then ready
        to send and receive WebSocket protocol frames and messages.

        If the WebSocket HTTP Upgrade request cannot be satisfied,
        a HTTP response is sent indicating the reason and status
        code (typically 400, "Bad Request"), and an appropriate
        exception will be thrown.

        The call blocks until one of the following conditions is true:

        @li An error occurs on the stream.

        @li The entire HTTP response has been sent.

        @throws boost::system::system_error Thrown on failure.
    */
    void
    accept();

    /** Read and respond to a WebSocket HTTP Upgrade request.

        This function is used to synchronously read a HTTP WebSocket
        Upgrade request and send the HTTP response.

        If the contents of the request are valid, the HTTP response
        indicates a successful upgrade and the stream is then ready
        to send and receive WebSocket protocol frames and messages.

        If the WebSocket HTTP Upgrade request cannot be satisfied,
        a HTTP response is sent indicating the reason and status
        code (typically 400, "Bad Request").

        The call blocks until one of the following conditions is true:

        @li An error occurs on the stream.

        @li The entire HTTP response has been sent.

        @param ec Set to indicate what error occurred, if any.
    */
    void
    accept(error_code& ec);

    /** Start reading and responding to a WebSocket HTTP Upgrade request.

        This function is used to asynchronously read a HTTP WebSocket
        Upgrade request and send the HTTP response. The function call
        always returns immediately.

        If the contents of the request are valid, the HTTP response
        indicates a successful upgrade and the stream is then ready
        to send and receive WebSocket protocol frames and messages.

        If the WebSocket HTTP Upgrade request cannot be satisfied,
        a HTTP response is sent indicating the reason and status
        code (typically 400, "Bad Request").

        @param handler The handler to be called when the request completes.
        Copies will be made of the handler as required. The equivalent
        function signature of the handler must be:
        @code void handler(
            error_code const& error // result of operation
        ); @endcode
        Regardless of whether the asynchronous operation completes
        immediately or not, the handler will not be invoked from within
        this function. Invocation of the handler will be performed in a
        manner equivalent to using boost::asio::io_service::post().
    */
    template<class AcceptHandler>
    typename async_completion<
        AcceptHandler, void(error_code)>::result_type
    async_accept(AcceptHandler&& handler);

    /** Read and respond to a WebSocket HTTP Upgrade request.

        This function is used to synchronously read a HTTP WebSocket
        Upgrade request and send the HTTP response.

        If the contents of the request are valid, the HTTP response
        indicates a successful upgrade and the stream is then ready
        to send and receive WebSocket protocol frames and messages.

        If the WebSocket HTTP Upgrade request cannot be satisfied,
        a HTTP response is sent indicating the reason and status
        code (typically 400, "Bad Request").

        The call blocks until one of the following conditions is true:

        @li An error occurs on the stream.

        @li The entire HTTP response has been sent.

        @param buffers Caller provide data that has already been
        received on the stream. This may be used for implementations
        allowing multiple protocols on the same stream. The
        buffered data will first be applied to the handshake, and
        then to received WebSocket frames. The implementation will
        copy the caller provided data before the function returns.

        @throws boost::system::system_error Thrown on failure.
    */
    template<class ConstBufferSequence>
    void
    accept(ConstBufferSequence const& buffers);

    /** Read and respond to a WebSocket HTTP Upgrade request.

        This function is used to synchronously read a HTTP WebSocket
        Upgrade request and send the HTTP response.

        If the contents of the request are valid, the HTTP response
        indicates a successful upgrade and the stream is then ready
        to send and receive WebSocket protocol frames and messages.

        If the WebSocket HTTP Upgrade request cannot be satisfied,
        a HTTP response is sent indicating the reason and status
        code (typically 400, "Bad Request").

        The call blocks until one of the following conditions is true:

        @li An error occurs on the stream.

        @li The entire HTTP response has been sent.

        @param buffers Caller provide data that has already been
        received on the stream. This may be used for implementations
        allowing multiple protocols on the same stream. The
        buffered data will first be applied to the handshake, and
        then to received WebSocket frames. The implementation will
        copy the caller provided data before the function returns.

        @param ec Set to indicate what error occurred, if any.
    */
    template<class ConstBufferSequence>
    void
    accept(ConstBufferSequence const& buffers, error_code& ec);

    /** Start reading and responding to a WebSocket HTTP Upgrade request.

        This function is used to asynchronously read a HTTP WebSocket
        Upgrade request and send the HTTP response. The function call
        always returns immediately.

        If the contents of the request are valid, the HTTP response
        indicates a successful upgrade and the stream is then ready
        to send and receive WebSocket protocol frames and messages.

        If the WebSocket HTTP Upgrade request cannot be satisfied,
        a HTTP response is sent indicating the reason and status
        code (typically 400, "Bad Request").

        @param buffers Caller provide data that has already been
        received on the stream. This may be used for implementations
        allowing multiple protocols on the same stream. The
        buffered data will first be applied to the handshake, and
        then to received WebSocket frames. The implementation will
        copy the caller provided data before the function returns.

        @param handler The handler to be called when the request completes.
        Copies will be made of the handler as required. The equivalent
        function signature of the handler must be:
        @code void handler(
            error_code const& error // result of operation
        ); @endcode
        Regardless of whether the asynchronous operation completes
        immediately or not, the handler will not be invoked from within
        this function. Invocation of the handler will be performed in a
        manner equivalent to using boost::asio::io_service::post().
    */
    template<class ConstBufferSequence, class AcceptHandler>
    typename async_completion<
        AcceptHandler, void(error_code)>::result_type
    async_accept(ConstBufferSequence const& buffers,
        AcceptHandler&& handler);

    /** Respond to a WebSocket HTTP Upgrade request

        This function is used to synchronously send the HTTP response
        to a HTTP WebSocket Upgrade request.

        If the contents of the request are valid, the HTTP response
        indicates a successful upgrade and the stream is then ready
        to send and receive WebSocket protocol frames and messages.

        If the WebSocket HTTP Upgrade request cannot be satisfied,
        a HTTP response is sent indicating the reason and status
        code (typically 400, "Bad Request").

        The call blocks until one of the following conditions is true:

        @li An error occurs on the stream.

        @li The entire HTTP response has been sent.

        @param request An object containing the HTTP Upgrade request. The
        implementation will make copies as necessary before this
        function call returns.

        @throws boost::system::system_error Thrown on failure.
    */
    // VFALCO TODO This should also take a streambuf with any leftover bytes.
    template<class Body, class Headers>
    void
    accept(http::request_v1<Body, Headers> const& request);

    /** Respond to a WebSocket HTTP Upgrade request

        This function is used to synchronously send the HTTP response
        to a HTTP WebSocket Upgrade request.

        If the contents of the request are valid, the HTTP response
        indicates a successful upgrade and the stream is then ready
        to send and receive WebSocket protocol frames and messages.

        If the WebSocket HTTP Upgrade request cannot be satisfied,
        a HTTP response is sent indicating the reason and status
        code (typically 400, "Bad Request").

        The call blocks until one of the following conditions is true:

        @li An error occurs on the stream.

        @li The entire HTTP response has been sent.

        @param request An object containing the HTTP Upgrade request. The
        implementation will make copies as necessary before this
        function call returns.

        @param ec Set to indicate what error occurred, if any.
    */
    template<class Body, class Headers>
    void
    accept(http::request_v1<Body, Headers> const& request,
        error_code& ec);

    /** Start reading and responding to a WebSocket HTTP Upgrade request.

        This function is used to asynchronously read a HTTP WebSocket
        Upgrade request and send the HTTP response. The function call
        always returns immediately.

        If the contents of the request are valid, the HTTP response
        indicates a successful upgrade and the stream is then ready
        to send and receive WebSocket protocol frames and messages.

        If the WebSocket HTTP Upgrade request cannot be satisfied,
        a HTTP response is sent indicating the reason and status
        code (typically 400, "Bad Request").

        @param request An object containing the HTTP Upgrade request. The
        implementation will make copies as necessary before this
        function call returns.

        @param handler The handler to be called when the request completes.
        Copies will be made of the handler as required. The equivalent
        function signature of the handler must be:
        @code void handler(
            error_code const& error // result of operation
        ); @endcode
        Regardless of whether the asynchronous operation completes
        immediately or not, the handler will not be invoked from within
        this function. Invocation of the handler will be performed in a
        manner equivalent to using boost::asio::io_service::post().
    */
    template<class Body, class Headers, class AcceptHandler>
    typename async_completion<
        AcceptHandler, void(error_code)>::result_type
    async_accept(http::request_v1<Body, Headers> const& request,
        AcceptHandler&& handler);

    /** Send a WebSocket Upgrade request.

        This function is used to synchronously send the WebSocket
        upgrade HTTP request. The call blocks until one of the
        following conditions is true:

        @li An error occurs on the stream

        @li A complete HTTP response with the result of the upgrade
        request is received.

        @throws boost::system::system_error Thrown on failure.

        @param host The name of the remote host, required by
        the HTTP protocol.

        @param resource The requesting URI, which may not be empty,
        required by the HTTP protocol.

        @par Example
        @code
        websocket::stream<ip::tcp::socket> ws(io_service);
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
    handshake(boost::string_ref const& host,
        boost::string_ref const& resource);

    /** Send a WebSocket Upgrade request.

        This function is used to synchronously send the WebSocket
        upgrade HTTP request. The call blocks until one of the
        following conditions is true:

        @li An error occurs on the stream.

        @li A complete HTTP response with the result of the upgrade
        request is received.

        @param host The name of the remote host, required by
        the HTTP protocol.

        @param resource The requesting URI, which may not be empty,
        required by the HTTP protocol.

        @param ec Set to indicate what error occurred, if any.

        @par Example
        @code
        websocket::stream<ip::tcp::socket> ws(io_service);
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
    handshake(boost::string_ref const& host,
        boost::string_ref const& resource, error_code& ec);

    /** Asynchronously send a WebSocket Upgrade request.

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
        Regardless of whether the asynchronous operation completes
        immediately or not, the handler will not be invoked from within
        this function. Invocation of the handler will be performed in a
        manner equivalent to using boost::asio::io_service::post().
    */
    template<class HandshakeHandler>
    typename async_completion<
        HandshakeHandler, void(error_code)>::result_type
    async_handshake(boost::string_ref const& host,
        boost::string_ref const& resource, HandshakeHandler&& h);

    /** Perform a WebSocket close.

        This function initiates the WebSocket close procedure.

        If the close reason specifies a close code other than
        close_code::none, the close frame is sent with the close code
        and optional reason string. Otherwise, the close frame
        is sent with no payload.

        Callers should not attempt to write WebSocket data after
        initiating the close. Instead, callers should continue
        reading until an error occurs. A read returning error::closed
        indicates a successful connection closure.

        @param cr The reason for the close.
    */
    void
    close(close_reason const& cr);

    /** Perform a WebSocket close.

        This function initiates the WebSocket close procedure.

        If the close reason specifies a close code other than
        close_code::none, the close frame is sent with the close code
        and optional reason string. Otherwise, the close frame
        is sent with no payload.

        Callers should not attempt to write WebSocket data after
        initiating the close. Instead, callers should continue
        reading until an error occurs. A read returning error::closed
        indicates a successful connection closure.

        @param cr The reason for the close.

        @param ec Set to indicate what error occurred, if any.
    */
    void
    close(close_reason const& cr, error_code& ec);

    /** Start an asychronous WebSocket close operation.

        This function initiates the WebSocket close procedure.

        If the close reason specifies a close code other than
        close_code::none, the close frame is sent with the close code
        and optional reason string. Otherwise, the close frame
        is sent with no payload.

        Callers should not attempt to write WebSocket data after
        initiating the close. Instead, callers should continue
        reading until an error occurs. A read returning error::closed
        indicates a successful connection closure.

        @param cr The reason for the close.

        @param handler The handler to be called when the close operation
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
    template<class CloseHandler>
    typename async_completion<
        CloseHandler, void(error_code)>::result_type
    async_close(close_reason const& cr, CloseHandler&& handler);

    /** Read a message.

        This function is used to read a message from the
        websocket. The function call will block until the message
        has been read successfully, or until an error occurs.

        On success op is set to reflect the message type, binary
        or text.

        @param op A value to receive the message type.
        This object must remain valid until the handler is called.

        @param streambuf A stream buffer to hold the message data.
        This object must remain valid until the handler is called.

        @throws boost::system::system_error Thrown on failure.
    */
    template<class Streambuf>
    void
    read(opcode& op, Streambuf& streambuf);

    /** Read a message.

        This function is used to read a message from the
        websocket. The function call will block until the message
        has been read successfully, or until an error occurs.

        On success op is set to reflect the message type, binary
        or text.

        @param op A value to receive the message type.
        This object must remain valid until the handler is called.

        @param streambuf A stream buffer to hold the message data.
        This object must remain valid until the handler is called.

        @param ec Set to indicate what error occurred, if any.
    */
    template<class Streambuf>
    void
    read(opcode& op,
        Streambuf& streambuf, error_code& ec);

    /** Start reading a message asynchronously.

        This function is used to asychronously read a message from
        the websocket. The function call always returns immediately.

        Upon a successful completion, op is set to either binary or
        text depending on the message type, and the input area of the
        streambuf will hold all the message payload bytes (which may
        be zero in length).

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
    template<class Streambuf, class ReadHandler>
    #if GENERATING_DOCS
    void_or_deduced
    #else
    typename async_completion<
        ReadHandler, void(error_code)>::result_type
    #endif
    async_read(opcode& op,
        Streambuf& streambuf, ReadHandler&& handler);

    /** Read a message frame.

        This function is used to read a single message frame from
        the websocket. The function call will block until one message
        frame has been read successfully, or an error occurs.

        On success, fi is filled out to reflect the message payload
        contents. op is set to binary or text, and the fin flag
        indicates if all the message data has been read in. To read the
        entire message, callers should repeat the read_frame operation
        until fi.fin is true. A message with no payload will have
        fi.fin == true, and zero bytes placed into the stream buffer.

        If a control frame is received while attempting to read a
        message frame, the control frame is handled automatically.
        If a ping control frame is received, a pong is sent immediately.
        If a close control frame is received, a close is sent in
        response and the read operation will return with `error::closed`.

        @param fi An object to store metadata about the message.

        @param streambuf A stream buffer to hold the message data.

        @throws boost::system::system_error Thrown on failure.
    */
    template<class Streambuf>
    void
    read_frame(frame_info& fi, Streambuf& streambuf);

    /** Read a message frame.

        This function is used to read a single message frame from
        the websocket. The function call will block until one message
        frame has been read successfully, or an error occurs.

        On success, fi is filled out to reflect the message payload
        contents. op is set to binary or text, and the fin flag
        indicates if all the message data has been read in. To read the
        entire message, callers should repeat the read_frame operation
        until fi.fin is true. A message with no payload will have
        fi.fin == true, and zero bytes placed into the stream buffer.

        If a control frame is received while attempting to read a
        message frame, the control frame is handled automatically.
        If a ping control frame is received, a pong is sent immediately.
        If a close control frame is received, a close is sent in
        response and the read operation will return with `error::closed`.

        @param fi An object to store metadata about the message.

        @param streambuf A stream buffer to hold the message data.

        @param ec Set to indicate what error occurred, if any.
    */
    template<class Streambuf>
    void
    read_frame(frame_info& fi, Streambuf& streambuf, error_code& ec);

    /** Start reading a message frame asynchronously.

        This function is used to asychronously read a single message
        frame from the websocket. The function call always returns
        immediately.

        Upon a successful completion, fi is filled out to reflect the
        message payload contents. op is set to binary or text, and the
        fin flag indicates if all the message data has been read in.
        To read the entire message, callers should repeat the
        read_frame operation until fi.fin is true. A message with no
        payload will have fi.fin == true, and zero bytes placed into
        the stream buffer.

        If a control frame is received while attempting to read a
        message frame, the control frame is handled automatically.
        If a ping control frame is received, a pong is sent immediately.
        If a close control frame is received, a close is sent in
        response and the read operation will return with `error::closed`.

        @param fi An object to store metadata about the message.
        This object must remain valid until the handler is called.

        @param streambuf A stream buffer to hold the message data after
        any masking or decompression has been applied. This object must
        remain valid until the handler is called.

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
    template<class Streambuf, class ReadHandler>
    typename async_completion<
        ReadHandler, void(error_code)>::result_type
    async_read_frame(frame_info& fi,
        Streambuf& streambuf, ReadHandler&& handler);

    /** Send a message.

        This function is used to write a message to the websocket.
        The call blocks until one of the following conditions is met:

        @li The entire message is sent.

        @li An error occurs.

        The message opcode will be set to text or binary as
        per the current setting of the `message_type` option.
        If automatic fragmenting is enabled, the message will be
        split into one or more frames as necessary.

        @param buffers The buffers containing the entire message
        payload. The implementation will make copies of this object
        as needed, but ownership of the underlying memory is not
        transferred. The caller is responsible for ensuring that
        the memory locations pointed to by buffers remains valid
        until the completion handler is called.

        @throws boost::system::system_error Thrown on failure.

        @note This function always sends an entire message. To
        send a message in fragments, use `write_frame`.
    */
    template<class ConstBufferSequence>
    void
    write(ConstBufferSequence const& buffers);

    /** Send a message.

        This function is used to write a message to the websocket.
        The call blocks until one of the following conditions is met:

        @li The entire message is sent.

        @li An error occurs.

        The message opcode will be set to text or binary as
        per the current setting of the `message_type` option.
        If automatic fragmenting is enabled, the message will be
        split into one or more frames as necessary.

        @param ec Set to indicate what error occurred, if any.

        @param buffers The buffers containing the entire message
        payload. The implementation will make copies of this object
        as needed, but ownership of the underlying memory is not
        transferred. The caller is responsible for ensuring that
        the memory locations pointed to by buffers remains valid
        until the completion handler is called.

        @note This function always sends an entire message. To
        send a message in fragments, use `write_frame`.
    */
    template<class ConstBufferSequence>
    void
    write(ConstBufferSequence const& buffers, error_code& ec);

    /** Start writing a complete message asynchronously.

        This function is used to asychronously write a message to
        the websocket. The function call always returns immediately.

        The message opcode will be set to text or binary as
        per the current setting of the `message_type` option.
        If automatic fragmenting is enabled, the message will be
        split into one or more frames as necessary.

        @param buffers The buffers containing the entire message
        payload. The implementation will make copies of this object
        as needed, but ownership of the underlying memory is not
        transferred. The caller is responsible for ensuring that
        the memory locations pointed to by buffers remains valid
        until the completion handler is called.

        @param handler The handler to be called when the write operation
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
    template<class ConstBufferSequence, class WriteHandler>
    #if GENERATING_DOCS
    void_or_deduced
    #else
    typename async_completion<
        WriteHandler, void(error_code)>::result_type
    #endif
    async_write(ConstBufferSequence const& buffers,
        WriteHandler&& handler);

    /** Send a frame.

        This function is used to write a frame to a stream. The
        call will block until one of the following conditions is true:

        @li All of the data in the supplied buffers has been written.

        @li An error occurs.

        This operation is implemented in terms of one or more calls
        to the stream's write_some function. The actual payload sent
        may be transformed as per the WebSocket protocol settings.

        If this is the beginning of a new message, the message opcode
        will be set to text or binary as per the current setting of
        the `message_type` option.

        @throws boost::system::system_error Thrown on failure.

        @param fin `true` if this is the last frame in the message.

        @param buffers One or more buffers containing the frame's
        payload data.
    */
    template<class ConstBufferSequence>
    void
    write_frame(bool fin, ConstBufferSequence const& buffers);

    /** Send a frame.

        This function is used to write a frame to a stream. The
        call will block until one of the following conditions is true:

        @li All of the data in the supplied buffers has been written.

        @li An error occurs.

        This operation is implemented in terms of one or more calls
        to the stream's write_some function. The actual payload sent
        may be transformed as per the WebSocket protocol settings.

        If this is the beginning of a new message, the message opcode
        will be set to text or binary as per the current setting of
        the `message_type` option.

        @param fin `true` if this is the last frame in the message.

        @param buffers One or more buffers containing the frame's
        payload data.

        @param ec Set to indicate what error occurred, if any.
    */
    template<class ConstBufferSequence>
    void
    write_frame(bool fin,
        ConstBufferSequence const& buffers, error_code& ec);

    /** Start sending a frame asynchronously.

        This function is used to asynchronously write a WebSocket
        frame on the stream. This function call always returns
        immediately.

        If this is the beginning of a new message, the message opcode
        will be set to text or binary as per the current setting of
        the `message_type` option.

        This operation is implemented in terms of one or more calls
        to the stream's async_write_some function. The actual payload
        sent may be transformed as per the WebSocket protocol settings.

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
    typename async_completion<
        WriteHandler, void(error_code)>::result_type
    async_write_frame(bool fin,
        ConstBufferSequence const& buffers, WriteHandler&& handler);

private:
    template<class Handler> class accept_op;
    template<class Handler> class close_op;
    template<class Handler> class handshake_op;
    template<class Handler> class response_op;
    template<class Streambuf, class Handler> class read_op;
    template<class Streambuf, class Handler> class read_frame_op;
    template<class Buffers, class Handler> class write_op;
    template<class Buffers, class Handler> class write_frame_op;

    http::request_v1<http::empty_body>
    build_request(boost::string_ref const& host,
        boost::string_ref const& resource,
            std::string& key);

    template<class Body, class Headers>
    http::response_v1<http::string_body>
    build_response(http::request_v1<Body, Headers> const& req);

    template<class Body, class Headers>
    void
    do_response(http::response_v1<Body, Headers> const& resp,
        boost::string_ref const& key, error_code& ec);

    void
    do_read_fh(detail::frame_streambuf& fb,
        close_code::value& code, error_code& ec);
};

} // websocket
} // beast

#include <beast/websocket/impl/stream.ipp>

#endif
