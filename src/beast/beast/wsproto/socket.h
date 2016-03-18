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

#ifndef BEAST_WSPROTO_STREAM_H_INCLUDED
#define BEAST_WSPROTO_STREAM_H_INCLUDED

#include <beast/wsproto/error.h>
#include <beast/wsproto/http.h>
#include <beast/wsproto/role.h>
#include <beast/wsproto/detail/frame.h>
#include <beast/wsproto/detail/mask.h>
#include <beast/http/message.h>
#include <beast/http/parser.h>
#include <beast/is_call_possible.h>
#include <boost/asio.hpp>
#include <memory>
#include <type_traits>

namespace beast {
namespace wsproto {

template<class T, std::size_t Size>
class small_object_ptr
{
    T* t_ = nullptr;
    std::unique_ptr<T> p_;
    std::array<std::uint8_t, Size> buf_;

public:
    small_object_ptr() = default;

    ~small_object_ptr()
    {
        clear();
    }

    template<class U, class... Args>
    void
    emplace(Args&&... args)
    {
        clear();
        if(sizeof(U) <= Size)
        {
            p_ = nullptr;
            t_ = new(buf_.data()) U(
                std::forward<Args>(args)...);
            return;
        }
        auto u = std::make_unique<U>(
            std::forward<Args>(args)...);
        t_ = u.get();
        p_ = std::move(u);
    }

    T* get()
    {
        return t_;
    }

    T const* get() const
    {
        return t_;
    }

    T* operator->()
    {
        return get();
    }

    T const* operator->() const
    {
        return get();
    }

    explicit
    operator bool() const
    {
        return get() != nullptr;
    }

private:
    void
    clear()
    {
        if(! t_)
            return;
        if(p_)
            p_ = nullptr;
        else
            t_->~T();
        t_ = nullptr;
    }
};

//------------------------------------------------------------------------------

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

namespace detail {
struct text_type {};
struct binary_type {};
} // detail

/// Text payload type option.
/**
    Sets the payload type to text (the default).
*/
static detail::text_type constexpr text{};

/// Binary payload type option.
/**
    Sets the payload type to binary.
*/
static detail::binary_type constexpr binary{};

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

//------------------------------------------------------------------------------

namespace detail {

struct at_most
{
    std::size_t n;
    
    explicit
    at_most(std::size_t n_)
        : n(n_)
    {
    }

    std::size_t
    operator()(error_code,
        std::size_t bytes_transferred)
    {
        if(bytes_transferred >= n)
            return 0;
        return n - bytes_transferred;
    }
};

template<class String>
inline
void
maybe_throw(error_code const& ec, String const&)
{
    if(ec)
        throw boost::system::system_error{ec};
}


class socket_base
{
protected:
    struct abstract_decorator
    {
        virtual ~abstract_decorator() = default;
        
        virtual
        void
        operator()(beast::http::message& m) const = 0;
    };

    template<class Decorator>
    struct decorator : abstract_decorator
    {
        Decorator d;

        template<class DeducedDecorator>
        decorator(DeducedDecorator&& d_)
            : d(std::forward<DeducedDecorator>(d_))
        {
        }

        void
        operator()(beast::http::message& m) const override
        {
            d(m);
        }
    };

    template<class = void>
    error_code
    prepare_fh();

    template<class Streambuf>
    void
    write_close(Streambuf& sb,
        close::value code, std::string reason);

    detail::maskgen maskgen_;
    std::unique_ptr<abstract_decorator> decorate_;
    bool keep_alive_ = false;
    role_type role_;

    // read state
    frame_header rd_fh_;
    detail::prepared_key_type rd_key_;
    std::size_t rd_need_ = 0;
    opcode::value rd_op_;
    bool rd_cont_ = false;
    bool rd_active_ = false;

    // write state
    opcode::value wr_op_ = opcode::text;
    std::size_t wr_frag_ = 0;
    bool wr_active_ = false;

    bool closing_ = false;
};

} // detail

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
class socket : public detail::socket_base
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
    set_option(detail::text_type)
    {
        wr_op_ = opcode::text;
    }

    void
    set_option(detail::binary_type)
    {
        wr_op_ = opcode::binary;
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
    decorate(Decorator&& d)
    {
        static_assert(beast::is_call_possible<Decorator,
            void(beast::http::message&)>::value,
                "Type does not meet the decorator requirements");
        decorate_ = std::make_unique<
            decorator<std::decay_t<Decorator>>>(
                std::forward<Decorator>(d));
    }

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
    template<class Handler>
    void
    async_handshake(std::string const& host,
        std::string const& resource, Handler&& h);

    /// Read and respond to a WebSocket HTTP Upgrade request.
    /**
        TBD
    */
    void
    accept()
    {
        error_code ec;
        accept(boost::asio::null_buffers{}, ec);
        detail::maybe_throw(ec, "accept");
    }

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
    accept(ConstBufferSequence const& buffers)
    {
        error_code ec;
        accept(buffers, ec);
        detail::maybe_throw(ec, "accept");
    }

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
    accept(beast::http::message const& m)
    {
        error_code ec;
        accept(m, ec);
        detail::maybe_throw(ec, "accept");
    }

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
    template<class Handler>
    void
    async_accept(Handler&& handler)
    {
        async_accept(boost::asio::null_buffers{},
            std::forward<Handler>(handler));
    }

    template<class ConstBufferSequence, class Handler>
    void
    async_accept(ConstBufferSequence&& buffers,
        Handler&& handler);

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
    template<class Handler>
    void
    async_accept_request(beast::http::message const& m,
        Handler&& h);

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
        This function initiates the WebSocket close procedure.
    */
    template<class Handler>
    void
    async_close(std::uint16_t code,
        std::string const& reason, Handler&& handler);

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
    template<class Handler>
    void
    async_read(frame_header& fh, Handler&& handler);

    /// Read frame payload data.
    /**
        This function is used to read the WebSocket frame payload data
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

    /// Read frame payload data.
    /**
        This function is used to read the WebSocket frame payload data
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

    /// Start reading frame payload data asynchronously.
    /**
        This function is used to asynchronously read the WebSocket
        frame payload data on the stream. This function call always
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
    template<class MutableBufferSequence, class Handler>
    void
    async_read(MutableBufferSequence&& buffers, Handler&& handler);

    /// Write an entire frame to a stream before returning.
    /**
        This function is used to write a frame to a stream. The
        call will block until one of the following conditions is true:

        @li All of the data in the supplied buffers has been written.

        @li An error occurs.

        This operation is implemented in terms of one or more calls
        to the stream's write_some function. The actual payload sent
        may be transformed as per the WebSocket protocol settings.

        @param fin `true` if this is the last frame in the message.

        @param buffers One or more buffers containing the frame's
        payload data.

        @param ec Set to indicate what error occurred, if any.
    */
    template<class ConstBufferSequence>
    void
    write(bool fin,
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
    write(bool fin, ConstBufferSequence const& buffers)
    {
        error_code ec;
        write(fin, buffers, ec);
        detail::maybe_throw(ec, "write");
    }

    /// Start writing a frame asynchronously
    /**
        This function is used to asynchronously write a WebSocket
        frame on the stream. This function call always returns
        immediately.

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
    template<class ConstBufferSequence, class Handler>
    void
    async_write(bool fin,
        ConstBufferSequence&& buffers, Handler&& handler);

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
};

//------------------------------------------------------------------------------

/// Read a complete WebSocket message.
/*
    TODO
*/
template<class Stream, class Streambuf>
void
read_msg(socket<Stream>& ws, Streambuf& sb)
{
    error_code ec;
    read_msg(ws, sb, ec);
    detail::maybe_throw(ec, "read_msg");
}

/// Read a complete WebSocket message.
/*
    TODO
*/
template<class Stream, class Streambuf>
void
read_msg(socket<Stream>& ws, Streambuf& sb, error_code& ec);

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
template<class Stream, class Streambuf, class Handler>
void
async_read_msg(socket<Stream>& ws, Streambuf& sb, Handler&& handler);

/// Write a complete WebSocket message.
/*
*/
template<class Stream, class ConstBufferSequence>
void
write_msg(socket<Stream>& ws, ConstBufferSequence const& buffers)
{
    error_code ec;
    write_msg(ws, buffers, ec);
    detail::maybe_throw(ec, "write_msg");
}

/// Write a complete WebSocket message.
/*
*/
template<class Stream, class ConstBufferSequence>
void
write_msg(socket<Stream>& ws,
    ConstBufferSequence const& buffers, error_code& ec);

template<class Stream, class ConstBufferSequence, class Handler>
void
async_write_msg(socket<Stream>& ws,
    ConstBufferSequence&& buffers, Handler&& handler);

} // wsproto
} // beast

#include <beast/wsproto/impl/socket.ipp>

#endif
