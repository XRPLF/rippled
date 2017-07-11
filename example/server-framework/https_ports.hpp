//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_EXAMPLE_SERVER_HTTPS_PORTS_HPP
#define BEAST_EXAMPLE_SERVER_HTTPS_PORTS_HPP

#include "http_sync_port.hpp"
#include "http_async_port.hpp"

#include "../common/ssl_stream.hpp"

#include <boost/asio/ssl.hpp>

namespace framework {

//------------------------------------------------------------------------------

// This class represents a synchronous HTTP connection which
// uses an OpenSSL socket as the stream.
//
template<class... Services>
class sync_https_con

    // Give this object the enable_shared_from_this, and have
    // the base class call impl().shared_from_this(). The reason
    // is so that the shared_ptr has the correct type. This lets
    // the derived class (this class) use its members in calls to
    // `std::bind`, without an ugly call to `dynamic_downcast` or
    // other nonsense.
    //
    : public std::enable_shared_from_this<sync_https_con<Services...>>

    // The stream should be created before the base class so
    // use the "base from member" idiom.
    //
    , public base_from_member<ssl_stream<socket_type>>

    // Constructs last, destroys first
    //
    , public sync_http_con_base<sync_https_con<Services...>, Services...>
{
public:
    // Constructor
    //
    // Additional arguments are forwarded to the base class
    //
    template<class... Args>
    sync_https_con(
        socket_type&& sock,
        boost::asio::ssl::context& ctx,
        Args&&... args)
        : base_from_member<ssl_stream<socket_type>>(std::move(sock), ctx)
        , sync_http_con_base<sync_https_con<Services...>, Services...>(
            std::forward<Args>(args)...)
    {
    }

    // Returns the stream.
    //
    // The base class calls this to obtain the object to use for
    // reading and writing HTTP messages. This allows the same base
    // class to work with different return types for `stream()` such
    // as a `boost::asio::ip::tcp::socket&` or a `boost::asio::ssl::stream&`
    //
    ssl_stream<socket_type>&
    stream()
    {
        return this->member;
    }

private:
    friend class sync_http_con_base<sync_https_con<Services...>, Services...>;

    // This is called by the base before running the main loop.
    //
    void
    do_handshake(error_code& ec)
    {
        // Perform the SSL handshake
        //
        stream().handshake(boost::asio::ssl::stream_base::server, ec);
    }

    // This is called when the other end closes the connection gracefully.
    //
    void
    do_shutdown(error_code& ec)
    {
        // Note that this is an SSL shutdown
        //
        stream().shutdown(ec);
        if(ec)
            return this->fail("ssl_shutdown", ec);
    }
};

//------------------------------------------------------------------------------

// This class represents an asynchronous HTTP connection which
// uses an OpenSSL socket as the stream.
//
template<class... Services>
class async_https_con

    // Give this object the enable_shared_from_this, and have
    // the base class call impl().shared_from_this(). The reason
    // is so that the shared_ptr has the correct type. This lets
    // the derived class (this class) use its members in calls to
    // `std::bind`, without an ugly call to `dynamic_downcast` or
    // other nonsense.
    //
    : public std::enable_shared_from_this<async_https_con<Services...>>

    // The stream should be created before the base class so
    // use the "base from member" idiom.
    //
    , public base_from_member<ssl_stream<socket_type>>

    // Constructs last, destroys first
    //
    , public async_http_con_base<async_https_con<Services...>, Services...>
{
public:
    // Constructor
    //
    // Additional arguments are forwarded to the base class
    //
    template<class... Args>
    async_https_con(
        socket_type&& sock,
        boost::asio::ssl::context& ctx,
        Args&&... args)
        : base_from_member<ssl_stream<socket_type>>(std::move(sock), ctx)
        , async_http_con_base<async_https_con<Services...>, Services...>(
            std::forward<Args>(args)...)
    {
    }

    // Returns the stream.
    //
    // The base class calls this to obtain the object to use for
    // reading and writing HTTP messages. This allows the same base
    // class to work with different return types for `stream()` such
    // as a `boost::asio::ip::tcp::socket&` or a `boost::asio::ssl::stream&`
    //
    ssl_stream<socket_type>&
    stream()
    {
        return this->member;
    }

    // Called by the multi-port after reading some
    // bytes from the stream and detecting SSL.
    // 
    template<class ConstBufferSequence>
    void
    handshake(ConstBufferSequence const& buffers)
    {
        // Copy the caller's bytes into the buffer we
        // use for reading HTTP messages, otherwise
        // the memory pointed to by buffers will go out
        // of scope.
        //
        this->buffer_.commit(
            boost::asio::buffer_copy(
                this->buffer_.prepare(boost::asio::buffer_size(buffers)),
                buffers));

        // Perform SSL handshake. We use the "buffered"
        // overload which lets us pass those extra bytes.
        //
        stream().async_handshake(
            boost::asio::ssl::stream_base::server,
            buffers,
            this->strand_.wrap(
                std::bind(
                    &async_https_con::on_buffered_handshake,
                    this->shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2)));
    }

private:
    friend class async_http_con_base<async_https_con<Services...>, Services...>;

    // Called by the base class before starting the main loop.
    //
    void
    do_handshake()
    {
        // This is SSL so perform the handshake
        //
        stream().async_handshake(
            boost::asio::ssl::stream_base::server,
            this->strand_.wrap(
                std::bind(
                    &async_https_con::on_handshake,
                    this->shared_from_this(),
                    std::placeholders::_1)));
    }

    // Called when the SSL handshake completes
    void
    on_handshake(error_code ec)
    {
        if(ec)
            return this->fail("on_handshake", ec);

        // No error so run the main loop
        this->do_run();
    }

    // Called when the buffered SSL handshake completes
    void
    on_buffered_handshake(error_code ec, std::size_t bytes_transferred)
    {
        if(ec)
            return this->fail("on_handshake", ec);

        // Consume what was read but leave the rest
        this->buffer_.consume(bytes_transferred);

        // No error so run the main loop
        this->do_run();
    }

    // Called when the end of stream is reached
    void
    do_shutdown()
    {
        // This is an SSL shutdown
        //
        stream().async_shutdown(
            this->strand_.wrap(
                std::bind(
                    &async_https_con::on_shutdown,
                    this->shared_from_this(),
                    std::placeholders::_1)));
    }

    // Called when the SSL shutdown completes
    void
    on_shutdown(error_code ec)
    {
        if(ec)
            return this->fail("on_shutdown", ec);
    }
};

//------------------------------------------------------------------------------

/*  A synchronous HTTPS port handler

    This type meets the requirements of @b PortHandler. It supports
    variable list of HTTP services in its template parameter list,
    and provides a synchronous connection implementation to service
*/
template<class... Services>
class https_sync_port
{
    // Reference to the server instance that made us
    server& instance_;

    // The stream to log to
    std::ostream& log_;

    // The list of services connections created from this port will support
    service_list<Services...> services_;

    // The SSL context containing the server's credentials
    boost::asio::ssl::context& ctx_;

public:
    /** Constructor

        @param instance The server instance which owns this port

        @param log The stream to use for logging

        @param ctx The SSL context holding the SSL certificates to use
    */
    https_sync_port(
        server& instance,
        std::ostream& log,
        boost::asio::ssl::context& ctx)
        : instance_(instance)
        , log_(log)
        , ctx_(ctx)
    {
    }

    /** Initialize a service

        Every service in the list must be initialized exactly once.

        @param args Optional arguments forwarded to the service
        constructor.

        @tparam Index The 0-based index of the service to initialize.

        @return A reference to the service list. This permits
        calls to be chained in a single expression.
    */
    template<std::size_t Index, class... Args>
    void
    init(error_code& ec, Args&&... args)
    {
        services_.template init<Index>(
            ec,
            std::forward<Args>(args)...);
    }

    /** Called by the server to provide ownership of the socket for a new connection

        @param sock The socket whose ownership is to be transferred

        @ep The remote endpoint
    */
    void
    on_accept(socket_type&& sock, endpoint_type ep)
    {
        // Create an HTTPS connection object
        // and transfer ownership of the socket.
        //
        std::make_shared<sync_https_con<Services...>>(
            std::move(sock),
            ctx_,
            "https_sync_port",
            log_,
            services_,
            instance_.next_id(),
            ep)->run();
    }
};

//------------------------------------------------------------------------------

/*  An asynchronous HTTPS port handler

    This type meets the requirements of @b PortHandler. It supports
    variable list of HTTP services in its template parameter list,
    and provides a synchronous connection implementation to service
*/
template<class... Services>
class https_async_port
{
    // Reference to the server instance that made us
    server& instance_;

    // The stream to log to
    std::ostream& log_;

    // The list of services connections created from this port will support
    service_list<Services...> services_;

    // The SSL context containing the server's credentials
    boost::asio::ssl::context& ctx_;

public:
    /** Constructor

        @param instance The server instance which owns this port

        @param log The stream to use for logging
    */
    https_async_port(
        server& instance,
        std::ostream& log,
        boost::asio::ssl::context& ctx)
        : instance_(instance)
        , log_(log)
        , ctx_(ctx)
    {
    }

    /** Initialize a service

        Every service in the list must be initialized exactly once.

        @param args Optional arguments forwarded to the service
        constructor.

        @tparam Index The 0-based index of the service to initialize.

        @return A reference to the service list. This permits
        calls to be chained in a single expression.
    */
    template<std::size_t Index, class... Args>
    void
    init(error_code& ec, Args&&... args)
    {
        services_.template init<Index>(
            ec,
            std::forward<Args>(args)...);
    }

    /** Called by the server to provide ownership of the socket for a new connection

        @param sock The socket whose ownership is to be transferred

        @ep The remote endpoint
    */
    void
    on_accept(socket_type&& sock, endpoint_type ep)
    {
        // Create an SSL connection object
        // and transfer ownership of the socket.
        //
        std::make_shared<async_https_con<Services...>>(
            std::move(sock),
            ctx_,
            "https_async_port",
            log_,
            services_,
            instance_.next_id(),
            ep)->run();
    }
};

} // framework

#endif
