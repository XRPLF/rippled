//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_EXAMPLE_SERVER_MULTI_PORT_HPP
#define BEAST_EXAMPLE_SERVER_MULTI_PORT_HPP

#include "ws_async_port.hpp"
#include "http_async_port.hpp"
#include "https_ports.hpp"
#include "wss_ports.hpp"

#include "../common/detect_ssl.hpp"

#include <beast/core.hpp>

#include <boost/function.hpp>

namespace framework {

// A connection that detects an opening SSL handshake
//
// If the SSL handshake is detected, then an HTTPS connection object
// is move constructed from this object. Otherwise, this object continues
// as a normal unencrypted HTTP connection. If the underlying port has
// the ws_upgrade_service configured, the connection may be optionally
// be upgraded to WebSocket by the client.
//
template<class... Services>
class multi_con

    // Give this object the enable_shared_from_this, and have
    // the base class call impl().shared_from_this(). The reason
    // is so that the shared_ptr has the correct type. This lets
    // the derived class (this class) use its members in calls to
    // `std::bind`, without an ugly call to `dynamic_downcast` or
    // other nonsense.
    //
    : public std::enable_shared_from_this<multi_con<Services...>>

    // The stream should be created before the base class so
    // use the "base from member" idiom.
    //
    , public base_from_member<socket_type>

    // Constructs last, destroys first
    //
    , public async_http_con_base<multi_con<Services...>, Services...>
{
    // Context to use if we get an SSL handshake
    boost::asio::ssl::context& ctx_;

    // Holds the data we read during ssl detection
    beast::static_buffer_n<6> buffer_;

public:
    // Constructor
    //
    // Additional arguments are simply forwarded to the base class
    //
    template<class... Args>
    multi_con(
        socket_type&& sock,
        boost::asio::ssl::context& ctx,
        Args&&... args)
        : base_from_member<socket_type>(std::move(sock))
        , async_http_con_base<multi_con<Services...>, Services...>(std::forward<Args>(args)...)
        , ctx_(ctx)
    {
    }

    // Returns the stream.
    //
    // The base class calls this to obtain the object to use for
    // reading and writing HTTP messages. This allows the same base
    // class to work with different return types for `stream()` such
    // as a `boost::asio::ip::tcp::socket&` or a `boost::asio::ssl::stream&`
    //
    socket_type&
    stream()
    {
        return this->member;
    }

    // Called by the port to launch the connection in detect mode
    void
    detect()
    {
        // The detect function operates asynchronously by reading
        // in some data from the stream to figure out if its an SSL
        // handshake. When it completes, it informs us of the result
        // and also stores the bytes it read in the buffer.
        //
        async_detect_ssl(
            stream(),
            buffer_,
            this->strand_.wrap(
                std::bind(
                    &multi_con::on_detect,
                    this->shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2)));
    }

private:
    // Base class needs to be a friend to call our private members
    friend class async_http_con_base<multi_con<Services...>, Services...>;

    // Called when the handshake detection is complete
    //
    void
    on_detect(
        error_code ec,
        boost::tribool result)
    {
        // Report failures if any
        if(ec)
            return this->fail("on_detect", ec);

        // Was an SSL handshake detected?
        if(result)
        {
            // Yes, get the remote endpoint since it is
            // needed to construct the new connection.
            //
            endpoint_type ep = stream().remote_endpoint(ec);
            if(ec)
                return this->fail("remote_endpoint", ec);

            // Now launch our new connection object
            //
            std::make_shared<async_https_con<Services...>>(
                std::move(stream()),
                ctx_,
                "multi_port",
                this->log_,
                this->services_,
                this->id_,
                ep)->handshake(buffer_.data());

            // When we return the last shared pointer to this
            // object will go away and `*this` will be destroyed.
            //
            return;
        }

        // No SSL handshake, so start the HTTP connection normally.
        //
        // Since we read some bytes from the connection that might
        // contain an HTTP request, we pass the buffer holding those
        // bytes to the base class so it can use them.
        //
        this->run(buffer_.data());
    }

    // This is called by the base before running the main loop.
    //
    void
    do_handshake()
    {
        // Just run the main loop right away.
        //
        this->do_run();
    }

    // This is called when the other end closes the connection gracefully.
    //
    void
    do_shutdown()
    {
        // Attempt a clean TCP/IP shutdown
        //
        error_code ec;
        stream().shutdown(
            socket_type::shutdown_both,
            ec);

        // not_connected happens under normal
        // circumstances so don't bother reporting it.
        //
        if(ec && ec != beast::errc::not_connected)
            return this->fail("shutdown", ec);
    }
};

//------------------------------------------------------------------------------

/*  An asynchronous HTTP and WebSocket port handler, plain or SSL

    This type meets the requirements of @b PortHandler. It supports a
    variable list of HTTP services in its template parameter list,
    and provides a synchronous connection implementation to service.

    The port will automatically detect OpenSSL handshakes and establish
    encrypted connections, otherwise will use a plain unencrypted
    connection. This all happens through the same port.

    In addition this port can process WebSocket upgrade requests by
    launching them as a new asynchronous WebSocket connection using
    either plain or OpenSSL transport.

    This class is split up into two parts, the multi_port_base,
    and the multi_port, to avoid a recursive type reference when
    we name the type of the ws_upgrade_service.
*/
class multi_port_base
{
protected:
    // VFALCO We use boost::function to work around a compiler
    //        crash with gcc and clang using libstdc++

    // The types of the on_stream callback
    using on_new_stream_cb1 = boost::function<void(beast::websocket::stream<socket_type>&)>;
    using on_new_stream_cb2 = boost::function<void(beast::websocket::stream<ssl_stream<socket_type>>&)>;

    // Reference to the server instance that made us
    server& instance_;

    // The stream to log to
    std::ostream& log_;

    // The context holds the SSL certificates the server uses
    boost::asio::ssl::context& ctx_;

    // Called for each new websocket stream
    on_new_stream_cb1 cb1_;
    on_new_stream_cb2 cb2_;

public:
    /** Constructor

        @param instance The server instance which owns this port

        @param log The stream to use for logging

        @param ctx The SSL context holding the SSL certificates to use

        @param cb A callback which will be invoked for every new
        WebSocket connection. This provides an opportunity to change
        the settings on the stream before it is used. The callback
        should have this equivalent signature:
        @code
        template<class NextLayer>
        void callback(beast::websocket::stream<NextLayer>&);
        @endcode
        In C++14 this can be accomplished with a generic lambda. In
        C++11 it will be necessary to write out a lambda manually,
        with a templated operator().
    */
    template<class Callback>
    multi_port_base(
        server& instance,
        std::ostream& log,
        boost::asio::ssl::context& ctx,
        Callback const& cb)
        : instance_(instance)
        , log_(log)
        , ctx_(ctx)
        , cb1_(cb)
        , cb2_(cb)
    {
    }

    /** Accept a WebSocket upgrade request.

        This is used to accept a connection that has already
        delivered the handshake.

        @param stream The stream corresponding to the connection.

        @param ep The remote endpoint.

        @param req The upgrade request.
    */
    template<class Body>
    void
    on_upgrade(
        socket_type&& sock,
        endpoint_type ep,
        beast::http::request<Body>&& req)
    {
        // Create the connection and call the version of
        // run that takes the request since we have it already
        //
        std::make_shared<async_ws_con>(
            std::move(sock),
            "multi_port",
            log_,
            instance_.next_id(),
            ep,
            cb1_
                )->run(std::move(req));
    }

    /** Accept a WebSocket upgrade request.

        This is used to accept a connection that has already
        delivered the handshake.

        @param stream The stream corresponding to the connection.

        @param ep The remote endpoint.

        @param req The upgrade request.
    */
    template<class Body>
    void
    on_upgrade(
        ssl_stream<socket_type>&& stream,
        endpoint_type ep,
        beast::http::request<Body>&& req)
    {
        std::make_shared<async_wss_con>(
            std::move(stream),
            "multi_port",
            log_,
            instance_.next_id(),
            ep,
            cb2_)->run(std::move(req));
    }
};

/*  An asynchronous HTTP and WebSocket port handler, plain or SSL

    This class is the other half of multi_port_base. It gets the
    Services... variadic type list and owns the service list.
*/
template<class... Services>
class multi_port : public multi_port_base
{
    // The list of services connections created from this port will support
    service_list<Services...> services_;

public:
    /** Constructor

        All arguments are forwarded to the multi_port_base constructor.
    */
    template<class... Args>
    multi_port(Args&&... args)
        : multi_port_base(std::forward<Args>(args)...)
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
    on_accept(
        socket_type&& sock,
        endpoint_type ep)
    {
        // Create a plain http connection object by transferring
        // ownership of the socket, then launch it to perform
        // the SSL handshake detection.
        //
        std::make_shared<multi_con<Services...>>(
            std::move(sock),
            ctx_,
            "multi_port",
            log_,
            services_,
            instance_.next_id(),
            ep)->detect();
    }
};

} // framework

#endif
