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

#ifndef BEAST_HTTP_STREAM_H_INCLUDED
#define BEAST_HTTP_STREAM_H_INCLUDED

#include <beast/core/async_completion.hpp>
#include <beast/core/basic_streambuf.hpp>
#include <beast/http.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/intrusive/list.hpp>
#include <memory>

namespace beast {
namespace http {

namespace detail {

class stream_base
{
protected:
    struct op
        : boost::intrusive::list_base_hook<
            boost::intrusive::link_mode<
                boost::intrusive::normal_link>>
    {
        virtual ~op() = default;
        virtual void operator()() = 0;
        virtual void cancel() = 0;
    };

    using op_list = typename boost::intrusive::make_list<
        op, boost::intrusive::constant_time_size<false>>::type;

    op_list wr_q_;
    bool wr_active_ = false;
};

} // detail

/** Provides message-oriented functionality using HTTP.

    The stream class template provides asynchronous and blocking
    message-oriented functionality necessary for clients and servers
    to utilize the HTTP protocol.

    @par Thread Safety
    @e Distinct @e objects: Safe.@n
    @e Shared @e objects: Unsafe. The application must ensure that
    all asynchronous operations are performed within the same
    implicit or explicit strand.

    @par Example

    To use the class template with an `ip::tcp::socket`, you would write:

    @code
    http::stream<ip::tcp::socket> hs(io_service);
    @endcode
    Alternatively, you can write:
    @code
    ip::tcp::socket sock(io_service);
    http::stream<ip::tcp::socket&> hs(sock);
    @endcode

    @note A stream object must not be destroyed while there are
    pending asynchronous operations associated with it.

    @par Concepts
    AsyncReadStream, AsyncWriteStream, Stream, SyncReadStream, SyncWriteStream.
 */
template<class NextLayer,
    class Allocator = std::allocator<char>>
class stream : public detail::stream_base
{
    NextLayer next_layer_;
    basic_streambuf<Allocator> rd_buf_;

public:
    /// The type of the next layer.
    using next_layer_type =
        typename std::remove_reference<NextLayer>::type;

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

    /** Destructor.

        @note A stream object must not be destroyed while there
        are pending asynchronous operations associated with it.
    */
    ~stream();

    /** Move constructor.

        Undefined behavior if operations are active or pending.
    */
    stream(stream&&) = default;

    /** Move assignment.

        Undefined behavior if operations are active or pending.
    */
    stream& operator=(stream&&) = default;

    /** Construct a HTTP stream.

        This constructor creates a HTTP stream and initialises
        the next layer.

        @throws Any exceptions thrown by the Stream constructor.

        @param args The arguments to be passed to initialise the
        next layer. The arguments are forwarded to the next layer's
        constructor.
    */
    template<class... Args>
    explicit
    stream(Args&&... args);

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

    /** Cancel pending operations.

        This will cancel all of the asynchronous operations pending,
        including pipelined writes that have not been started. Handlers for
        canceled writes will be called with
        `boost::asio::error::operation_aborted`.

        @throws boost::system::system_error Thrown on failure.
    */
    void
    cancel()
    {
        error_code ec;
        cancel(ec);
        if(ec)
            throw system_error{ec};
    }

    /** Cancel pending operations.

        This will cancel all of the asynchronous operations pending,
        including pipelined writes that have not been started. Handlers for
        canceled writes will be called with
        `boost::asio::error::operation_aborted`.

        @param ec Set to indicate what error occurred, if any.
    */
    void
    cancel(error_code& ec);

    /** Read a HTTP message from the stream.

        This function is used to read a single HTTP message from the stream.
        The call will block until one of the followign conditions is true:

        @li A message has been read.

        @li An error occurred.

        The operation is implemented in terms of zero or more calls to the
        next layer's `read_some` function.

        @param msg An object used to store the message. The previous
        contents of the object will be overwritten.

        @throws boost::system::system_error Thrown on failure.
    */
    template<bool isRequest, class Body, class Headers>
    void
    read(message_v1<isRequest, Body, Headers>& msg)
    {
        error_code ec;
        read(msg, ec);
        if(ec)
            throw system_error{ec};
    }

    /** Read a HTTP message from the stream.

        This function is used to read a single HTTP message from the stream.
        The call will block until one of the followign conditions is true:

        @li A message has been read.

        @li An error occurred.

        The operation is implemented in terms of zero or more calls to the
        next layer's `read_some` function.

        @param msg An object used to store the message. The previous
        contents of the object will be overwritten.

        @param ec Set to indicate what error occurred, if any.
    */
    template<bool isRequest, class Body, class Headers>
    void
    read(message_v1<isRequest, Body, Headers>& msg,
        error_code& ec);

    /** Start reading a HTTP message from the stream asynchronously.

        This function is used to asynchronously read a single HTTP message
        from the stream. The function call always returns immediately. The
        asynchronous operation will continue until one of the following
        conditions is true:

        @li The message has been written.

        @li An error occurred.

        This operation is implemented in terms of zero or more calls to the
        next layer's async_read_some function, and is known as a composed
        operation. The program must ensure that the stream performs no other
        read operations or any other composed operations that perform reads
        until this operation completes.

        @param msg An object used to store the message. The previous
        contents of the object will be overwritten. Ownership of the message
        is not transferred; the caller must guarantee that the object remains
        valid until the handler is called.

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
    template<bool isRequest, class Body, class Headers,
        class ReadHandler>
    #if GENERATING_DOCS
    void_or_deduced
    #else
    typename async_completion<
        ReadHandler, void(error_code)>::result_type
    #endif
    async_read(message_v1<isRequest, Body, Headers>& msg,
        ReadHandler&& handler);

    /** Write a HTTP message to the stream.

        This function is used to write a single HTTP message to the
        stream. The call will block until one of the following conditions
        is true:

        @li The entire message is sent.

        @li An error occurred.

        If the semantics of the message require that the connection is
        closed to indicate the end of the content body,
        `boost::asio::error::eof` is thrown after the message is sent.
        successfuly. The caller is responsible for actually closing the
        connection. For regular TCP/IP streams this means shutting down the
        send side, while SSL streams may call the SSL shutdown function.

        @param msg The message to send.

        @throws boost::system::system_error Thrown on failure.
    */
    template<bool isRequest, class Body, class Headers>
    void
    write(message_v1<isRequest, Body, Headers> const& msg)
    {
        error_code ec;
        write(msg, ec);
        if(ec)
            throw system_error{ec};
    }

    /** Write a HTTP message to the stream.

        This function is used to write a single HTTP message to the
        stream. The call will block until one of the following conditions
        is true:

        @li The entire message is sent.

        @li An error occurred.

        If the semantics of the message require that the connection is
        closed to indicate the end of the content body,
        `boost::asio::error::eof` is returned after the message is sent.
        successfuly. The caller is responsible for actually closing the
        connection. For regular TCP/IP streams this means shutting down the
        send side, while SSL streams may call the SSL shutdown function.

        @param msg The message to send.

        @param ec Set to the error, if any occurred.
    */
    template<bool isRequest, class Body, class Headers>
    void
    write(message_v1<isRequest, Body, Headers> const& msg,
        error_code& ec);

    /** Start pipelining a HTTP message to the stream asynchronously.

        This function is used to queue a message to be sent on the stream.
        Unlike the free function, this version will place the message on an
        outgoing message queue if there is already a write pending.

        If the semantics of the message require that the connection is
        closed to indicate the end of the content body, the handler
        is called with the error `boost::asio::error::eof` after the message
        has been sent successfully. The caller is responsible for actually
        closing the connection. For regular TCP/IP streams this means
        shutting down the send side, while SSL streams may call the SSL
        `async_shutdown` function.

        @param msg The message to send. A copy of the message will be made.

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
    template<bool isRequest, class Body, class Headers,
        class WriteHandler>
    #if GENERATING_DOCS
    void_or_deduced
    #else
    typename async_completion<
        WriteHandler, void(error_code)>::result_type
    #endif
    async_write(message_v1<isRequest, Body, Headers> const& msg,
        WriteHandler&& handler);

    /** Start pipelining a HTTP message to the stream asynchronously.

        This function is used to queue a message to be sent on the stream.
        Unlike the free function, this version will place the message on an
        outgoing message queue if there is already a write pending.

        If the semantics of the message require that the connection is
        closed to indicate the end of the content body, the handler
        is called with the error boost::asio::error::eof. The caller is
        responsible for actually closing the connection. For regular
        TCP/IP streams this means shutting down the send side, while SSL
        streams may call the SSL async_shutdown function.

        @param msg The message to send. Ownership of the message, which
        must be movable, is transferred to the implementation. The message
        will not be destroyed until the asynchronous operation completes.

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
    template<bool isRequest, class Body, class Headers,
        class WriteHandler>
    #if GENERATING_DOCS
    void_or_deduced
    #else
    typename async_completion<
        WriteHandler, void(error_code)>::result_type
    #endif
    async_write(message_v1<isRequest, Body, Headers>&& msg,
        WriteHandler&& handler);

private:
    template<bool, class, class, class> class read_op;
    template<bool, class, class, class> class write_op;

    void
    cancel_all();
};

} // http
} // beast

#include "http_stream.ipp"

#endif
