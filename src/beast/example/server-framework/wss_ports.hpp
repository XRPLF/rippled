//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_EXAMPLE_SERVER_WSS_PORTS_HPP
#define BEAST_EXAMPLE_SERVER_WSS_PORTS_HPP

#include "ws_sync_port.hpp"
#include "ws_async_port.hpp"

#include "../common/ssl_stream.hpp"

#include <boost/asio/ssl.hpp>
#include <boost/function.hpp>

namespace framework {

//------------------------------------------------------------------------------

// A synchronous WebSocket connection over an SSL connection
//
class sync_wss_con

    // Give this object the enable_shared_from_this, and have
    // the base class call impl().shared_from_this(). The reason
    // is so that the shared_ptr has the correct type. This lets
    // the derived class (this class) use its members in calls to
    // `std::bind`, without an ugly call to `dynamic_downcast` or
    // other nonsense.
    //
    : public std::enable_shared_from_this<sync_wss_con>

    // The stream should be created before the base class so
    // use the "base from member" idiom.
    //
    , public base_from_member<beast::websocket::stream<ssl_stream<socket_type>>>

    // Constructs last, destroys first
    //
    , public sync_ws_con_base<sync_wss_con>
{
public:
    // Constructor
    //
    // Additional arguments are forwarded to the base class
    //
    template<class... Args>
    explicit
    sync_wss_con(
        socket_type&& sock,
        boost::asio::ssl::context& ctx,
        Args&&... args)
        : base_from_member<beast::websocket::stream<ssl_stream<socket_type>>>(std::move(sock), ctx)
        , sync_ws_con_base<sync_wss_con>(std::forward<Args>(args)...)
    {
    }

    // Construct from an existing, handshaked SSL stream
    //
    template<class... Args>
    sync_wss_con(
        ssl_stream<socket_type>&& stream,
        Args&&... args)
        : base_from_member<beast::websocket::stream<ssl_stream<socket_type>>>(std::move(stream))
        , sync_ws_con_base<sync_wss_con>(std::forward<Args>(args)...)
    {
    }

    // Returns the stream.
    //
    // The base class calls this to obtain the object to use for
    // reading and writing HTTP messages. This allows the same base
    // class to work with different return types for `stream()` such
    // as a `boost::asio::ip::tcp::socket&` or a `boost::asio::ssl::stream&`
    //
    beast::websocket::stream<ssl_stream<socket_type>>&
    stream()
    {
        return this->member;
    }

private:
    friend class sync_ws_con_base<sync_wss_con>;

    // This is called by the base before running the main loop.
    //
    void
    do_handshake(error_code& ec)
    {
        // Perform the SSL handshake
        //
        // We use next_layer() to get at the underlying ssl_stream
        //
        stream().next_layer().handshake(boost::asio::ssl::stream_base::server, ec);
    }
};

//------------------------------------------------------------------------------

// An asynchronous WebSocket connection over an SSL connection
//
class async_wss_con

    // Give this object the enable_shared_from_this, and have
    // the base class call impl().shared_from_this(). The reason
    // is so that the shared_ptr has the correct type. This lets
    // the derived class (this class) use its members in calls to
    // `std::bind`, without an ugly call to `dynamic_downcast` or
    // other nonsense.
    //
    : public std::enable_shared_from_this<async_wss_con>

    // The stream should be created before the base class so
    // use the "base from member" idiom.
    //
    , public base_from_member<beast::websocket::stream<ssl_stream<socket_type>>>

    // Constructs last, destroys first
    //
    , public async_ws_con_base<async_wss_con>
{
public:
    // Constructor
    //
    // Additional arguments are forwarded to the base class
    //
    template<class... Args>
    async_wss_con(
        socket_type&& sock,
        boost::asio::ssl::context& ctx,
        Args&&... args)
        : base_from_member<beast::websocket::stream<ssl_stream<socket_type>>>(std::move(sock), ctx)
        , async_ws_con_base<async_wss_con>(std::forward<Args>(args)...)
    {
    }

    // Construct from an existing, handshaked SSL stream
    //
    template<class... Args>
    async_wss_con(
        ssl_stream<socket_type>&& stream,
        Args&&... args)
        : base_from_member<beast::websocket::stream<ssl_stream<socket_type>>>(std::move(stream))
        , async_ws_con_base<async_wss_con>(std::forward<Args>(args)...)
    {
    }

    // Returns the stream.
    //
    // The base class calls this to obtain the object to use for
    // reading and writing HTTP messages. This allows the same base
    // class to work with different return types for `stream()` such
    // as a `boost::asio::ip::tcp::socket&` or a `boost::asio::ssl::stream&`
    //
    beast::websocket::stream<ssl_stream<socket_type>>&
    stream()
    {
        return this->member;
    }

private:
    friend class async_ws_con_base<async_wss_con>;

    // Called by the port to start the connection
    // after creating the object
    //
    void
    do_handshake()
    {
        // This is SSL so perform the handshake first
        //
        stream().next_layer().async_handshake(
            boost::asio::ssl::stream_base::server,
            this->strand_.wrap(
                std::bind(
                    &async_wss_con::on_handshake,
                    this->shared_from_this(),
                    std::placeholders::_1)));
    }

    // Called when the SSL handshake completes
    //
    void
    on_handshake(error_code ec)
    {
        if(ec)
            return this->fail("on_handshake", ec);

        // Move on to accepting the WebSocket handshake
        //
        this->do_accept();
    }
};

//------------------------------------------------------------------------------

/** A synchronous Secure WebSocket @b PortHandler which implements echo.

    This is a port handler which accepts Secure WebSocket upgrade
    HTTP requests and implements the echo protocol. All received
    WebSocket messages will be echoed back to the remote host.
*/
class wss_sync_port
{
    // VFALCO We use boost::function to work around a compiler
    //        crash with gcc and clang using libstdc++

    // The types of the on_new_stream callbacks
    //
    using on_new_stream_cb1 =
        boost::function<void(beast::websocket::stream<socket_type>&)>;
    
    using on_new_stream_cb2 =
        boost::function<void(beast::websocket::stream<ssl_stream<socket_type>>&)>;

    server& instance_;
    std::ostream& log_;
    boost::asio::ssl::context& ctx_;
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
    wss_sync_port(
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

    /** Accept a TCP/IP connection.

        This function is called when the server has accepted an
        incoming connection.

        @param sock The connected socket.

        @param ep The endpoint of the remote host.
    */
    void
    on_accept(socket_type&& sock, endpoint_type ep)
    {
        // Create our connection object and run it
        //
        std::make_shared<sync_wss_con>(
            std::move(sock),
            ctx_,
            "wss_sync_port",
            log_,
            instance_.next_id(),
            ep,
            cb2_)->run();
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
        // Create the connection object and run it,
        // transferring ownership of the ugprade request.
        //
        std::make_shared<sync_wss_con>(
            std::move(stream),
            "wss_sync_port",
            log_,
            instance_.next_id(),
            ep,
            cb2_)->run(std::move(req));
    }
};

//------------------------------------------------------------------------------

/** An asynchronous WebSocket @b PortHandler which implements echo.

    This is a port handler which accepts WebSocket upgrade HTTP
    requests and implements the echo protocol. All received
    WebSocket messages will be echoed back to the remote host.
*/
class wss_async_port
{
    // VFALCO We use boost::function to work around a compiler
    //        crash with gcc and clang using libstdc++

    // The types of the on_new_stream callbacks
    //
    using on_new_stream_cb1 =
        boost::function<void(beast::websocket::stream<socket_type>&)>;

    using on_new_stream_cb2 =
        boost::function<void(beast::websocket::stream<ssl_stream<socket_type>>&)>;

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
    wss_async_port(
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

    /** Accept a TCP/IP connection.

        This function is called when the server has accepted an
        incoming connection.

        @param sock The connected socket.

        @param ep The endpoint of the remote host.
    */
    void
    on_accept(
        socket_type&& sock,
        endpoint_type ep)
    {
        std::make_shared<async_wss_con>(
            std::move(sock),
            ctx_,
            "wss_async_port",
            log_,
            instance_.next_id(),
            ep,
            cb2_)->run();
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
            "wss_async_port",
            log_,
            instance_.next_id(),
            ep,
            cb2_)->run(std::move(req));
    }
};

} // framework

#endif
