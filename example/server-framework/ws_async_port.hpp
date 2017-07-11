//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_EXAMPLE_SERVER_WS_ASYNC_PORT_HPP
#define BEAST_EXAMPLE_SERVER_WS_ASYNC_PORT_HPP

#include "server.hpp"

#include <beast/core/multi_buffer.hpp>
#include <beast/websocket/stream.hpp>
#include <boost/function.hpp>
#include <memory>
#include <ostream>

namespace framework {

// This object holds the state of the connection
// including, most importantly, the socket or stream.
//
//
template<class Derived>
class async_ws_con_base
{
    // This function lets us access members of the derived class
    Derived&
    impl()
    {
        return static_cast<Derived&>(*this);
    }

    // The string used to set the Server http field
    std::string server_name_;

    // The stream to use for logging
    std::ostream& log_;

    // A small unique integer for logging
    std::size_t id_;

    // The remote endpoint. We cache it here because
    // calls to remote_endpoint() can fail / throw.
    //
    endpoint_type ep_;

    // This is used to hold the message data
    beast::multi_buffer buffer_;

protected:
    // The strand makes sure that our data is
    // accessed from only one thread at a time.
    //
    strand_type strand_;

public:
    // Constructor
    template<class Callback>
    async_ws_con_base(
        beast::string_view server_name,
        std::ostream& log,
        std::size_t id,
        endpoint_type const& ep,
        Callback const& cb)
        : server_name_(server_name)
        , log_(log)
        , id_(id)
        , ep_(ep)

        // Limit of 1MB on messages
        , buffer_(1024 * 1024)

        , strand_(impl().stream().get_io_service())
    {
        cb(impl().stream());
    }

    // Run the connection
    //
    void
    run()
    {
        impl().do_handshake();
    }

    // Run the connection.
    //
    // This overload handles the case where we
    // already have the WebSocket Upgrade request.
    //
    template<class Body>
    void
    run(beast::http::request<Body> const& req)
    {
        // Call the overload of accept() which takes
        // the request by parameter, instead of reading
        // it from the network.
        //
        impl().stream().async_accept_ex(req,
            [&](beast::websocket::response_type& res)
            {
                res.set(beast::http::field::server, server_name_);
            },
            strand_.wrap(std::bind(
                &async_ws_con_base::on_accept,
                impl().shared_from_this(),
                std::placeholders::_1)));
    }

protected:
    // Performs the WebSocket handshake
    void
    do_accept()
    {
        // Read the WebSocket upgrade request and attempt
        // to send back the response.
        //
        impl().stream().async_accept_ex(
            [&](beast::websocket::response_type& res)
            {
                res.set(beast::http::field::server, server_name_);
            },
            strand_.wrap(std::bind(
                &async_ws_con_base::on_accept,
                impl().shared_from_this(),
                std::placeholders::_1)));
    }

    // This helper reports failures
    //
    void
    fail(std::string what, error_code ec)
    {
        if(ec != beast::websocket::error::closed)
            log_ <<
                "[#" << id_ << " " << ep_ << "] " <<
            what << ": " << ec.message() << std::endl;
    }

private:
    // Called when accept_ex completes
    //
    void
    on_accept(error_code ec)
    {
        if(ec)
            return fail("async_accept", ec);
        do_read();
    }

    // Read the next WebSocket message
    //
    void
    do_read()
    {
        impl().stream().async_read(
            buffer_,
            strand_.wrap(std::bind(
                &async_ws_con_base::on_read,
                impl().shared_from_this(),
                std::placeholders::_1)));
    }
        
    // Called when the message read completes
    //
    void
    on_read(error_code ec)
    {
        if(ec)
            return fail("on_read", ec);

        // Set the outgoing message type. We will use
        // the same setting as the message we just read.
        //
        impl().stream().binary(impl().stream().got_binary());

        // Now echo back the message
        //
        impl().stream().async_write(
            buffer_.data(),
            strand_.wrap(std::bind(
                &async_ws_con_base::on_write,
                impl().shared_from_this(),
                std::placeholders::_1)));
    }

    // Called when the message write completes
    //
    void
    on_write(error_code ec)
    {
        if(ec)
            return fail("on_write", ec);

        // Empty out the contents of the message buffer
        // to prepare it for the next call to read.
        //
        buffer_.consume(buffer_.size());

        // Now read another message
        //
        do_read();
    }
};

//------------------------------------------------------------------------------

// This class represents an asynchronous WebSocket connection
// which uses a plain TCP/IP socket (no encryption) as the stream.
//
class async_ws_con

    // Give this object the enable_shared_from_this, and have
    // the base class call impl().shared_from_this(). The reason
    // is so that the shared_ptr has the correct type. This lets
    // the derived class (this class) use its members in calls to
    // `std::bind`, without an ugly call to `dynamic_downcast` or
    // other nonsense.
    //
    : public std::enable_shared_from_this<async_ws_con>

    // The stream should be created before the base class so
    // use the "base from member" idiom.
    //
    , public base_from_member<beast::websocket::stream<socket_type>>

    // Constructs last, destroys first
    //
    , public async_ws_con_base<async_ws_con>
{
public:
    // Constructor
    //
    // Additional arguments are forwarded to the base class
    //
    template<class... Args>
    explicit
    async_ws_con(
        socket_type&& sock,
        Args&&... args)
        : base_from_member<beast::websocket::stream<socket_type>>(std::move(sock))
        , async_ws_con_base<async_ws_con>(std::forward<Args>(args)...)
    {
    }

    // Returns the stream.
    //
    // The base class calls this to obtain the object to use for
    // reading and writing HTTP messages. This allows the same base
    // class to work with different return types for `stream()` such
    // as a `boost::asio::ip::tcp::socket&` or a `boost::asio::ssl::stream&`
    //
    beast::websocket::stream<socket_type>&
    stream()
    {
        return this->member;
    }

private:
    // Base class needs to be a friend to call our private members
    friend async_ws_con_base<async_ws_con>;

    void
    do_handshake()
    {
        do_accept();
    }
};

//------------------------------------------------------------------------------

/** An asynchronous WebSocket @b PortHandler which implements echo.

    This is a port handler which accepts WebSocket upgrade HTTP
    requests and implements the echo protocol. All received
    WebSocket messages will be echoed back to the remote host.
*/
class ws_async_port
{
    // The type of the on_new_stream callback
    //
    using on_new_stream_cb =
        boost::function<void(beast::websocket::stream<socket_type>&)>;

    server& instance_;
    std::ostream& log_;
    on_new_stream_cb cb_;

public:
    /** Constructor

        @param instance The server instance which owns this port

        @param log The stream to use for logging

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
    ws_async_port(
        server& instance,
        std::ostream& log,
        Callback const& cb)
        : instance_(instance)
        , log_(log)
        , cb_(cb)
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
        std::make_shared<async_ws_con>(
            std::move(sock),
            "ws_async_port",
            log_,
            instance_.next_id(),
            ep,
            cb_)->run();
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
        std::make_shared<async_ws_con>(
            std::move(sock),
            "ws_async_port",
            log_,
            instance_.next_id(),
            ep,
            cb_)->run(std::move(req));
    }
};

} // framework

#endif
