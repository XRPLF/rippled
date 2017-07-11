//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_EXAMPLE_SERVER_WS_SYNC_PORT_HPP
#define BEAST_EXAMPLE_SERVER_WS_SYNC_PORT_HPP

#include "server.hpp"

#include <beast/core/multi_buffer.hpp>
#include <beast/websocket.hpp>
#include <boost/function.hpp>
#include <memory>
#include <ostream>
#include <thread>

namespace framework {

/** A synchronous WebSocket connection.

    This base class implements a WebSocket connection object using
    synchronous calls over an unencrypted connection.

    It uses the Curiously Recurring Template pattern (CRTP) where
    we refer to the derived class in order to access the stream object
    to use for reading and writing. This lets the same class be used
    for plain and SSL stream objects.
*/
template<class Derived>
class sync_ws_con_base
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

public:
    // Constructor
    //
    // Additional arguments are forwarded to the base class
    //
    template<class Callback>
    sync_ws_con_base(
        beast::string_view server_name,
        std::ostream& log,
        std::size_t id,
        endpoint_type const& ep,
        Callback const& cb)
        : server_name_(server_name)
        , log_(log)
        , id_(id)
        , ep_(ep)
    {
        cb(impl().stream());
    }

    // Run the connection. This is called for the case
    // where we have not received the upgrade request yet.
    //
    void
    run()
    {
        // We run the do_run function in its own thread,
        // and bind a shared pointer to the connection object
        // into the function. The last reference to the shared
        // pointer will go away when the thread exits, thus
        // destroying the connection object.
        //
        std::thread{
            &sync_ws_con_base::do_accept,
            impl().shared_from_this()
        }.detach();
    }

    // Run the connection from an already-received Upgrade request.
    //
    template<class Body>
    void
    run(beast::http::request<Body>&& req)
    {
        BOOST_ASSERT(beast::websocket::is_upgrade(req));

        // We need to transfer ownership of the request object into
        // the lambda, but there's no C++14 lambda capture
        // so we have to write it out by manually specifying the lambda.
        //
        std::thread{
            lambda<Body>{
                impl().shared_from_this(),
                std::move(req)
        }}.detach();
    }

protected:
    // Called when a failure occurs
    //
    void
    fail(std::string what, error_code ec)
    {
        // Don't report the "closed" error since that
        // happens under normal circumstances.
        //
        if(ec && ec != beast::websocket::error::closed)
        {
            log_ <<
                "[#" << id_ << " " << ep_ << "] " <<
            what << ": " << ec.message() << std::endl;
            log_.flush();
        }
    }

private:
    // This function performs the WebSocket handshake
    // and runs the main loop upon success.
    void
    do_accept()
    {
        error_code ec;

        // Give the derived class a chance to do stuff before we
        // enter the main loop. This is for SSL connections really.
        //
        impl().do_handshake(ec);

        if(ec)
            return fail("handshake", ec);

        // Read the WebSocket upgrade request and attempt
        // to send back the response.
        //
        impl().stream().accept_ex(
            [&](beast::websocket::response_type& res)
            {
                res.insert(beast::http::field::server, server_name_);
            },
            ec);

        if(ec)
            return fail("accept", ec);

        // Run the connection
        //
        do_run();
    }

    // This is the lambda used when launching a connection from
    // an already-received request. In C++14 we could simply use
    // a lambda capture but this example requires only C++11 so
    // we write out the lambda ourselves. This is similar to what
    // the compiler would generate anyway.
    //
    template<class Body>
    class lambda
    {
        std::shared_ptr<sync_ws_con_base> self_;
        beast::http::request<Body> req_;
        
    public:
        // Constructor
        //
        // This is the equivalent of the capture section of the lambda.
        //
        lambda(
            std::shared_ptr<sync_ws_con_base> self,
            beast::http::request<Body>&& req)
            : self_(std::move(self))
            , req_(std::move(req))
        {
            BOOST_ASSERT(beast::websocket::is_upgrade(req_));
        }

        // Invoke the lambda
        //
        void
        operator()()
        {
            BOOST_ASSERT(beast::websocket::is_upgrade(req_));
            error_code ec;
            {
                // Move the message to the stack so we can get
                // rid of resources, otherwise it will linger
                // for the lifetime of the connection.
                //
                auto req = std::move(req_);

                // Call the overload of accept() which takes
                // the request by parameter, instead of reading
                // it from the network.
                //
                self_->impl().stream().accept_ex(req,
                    [&](beast::websocket::response_type& res)
                    {
                        res.insert(beast::http::field::server, self_->server_name_);
                    },
                    ec);
            }

            if(ec)
                return self_->fail("accept", ec);

            self_->do_run();
        }
    };

    void
    do_run()
    {
        error_code ec;

        // Loop, reading messages and echoing them back.
        //
        for(;;)
        {
            // This buffer holds the message. We place a one
            // megabyte limit on the size to prevent abuse.
            //
            beast::multi_buffer buffer{1024*1024};

            // Read the message
            //
            impl().stream().read(buffer, ec);

            if(ec)
                return fail("read", ec);

            // Set the outgoing message type. We will use
            // the same setting as the message we just read.
            //
            impl().stream().binary(impl().stream().got_binary());

            // Now echo back the message
            //
            impl().stream().write(buffer.data(), ec);

            if(ec)
                return fail("write", ec);
        }
    }
};

//------------------------------------------------------------------------------

// This class represents a synchronous WebSocket connection
// which uses a plain TCP/IP socket (no encryption) as the stream.
//
class sync_ws_con

    // Give this object the enable_shared_from_this, and have
    // the base class call impl().shared_from_this(). The reason
    // is so that the shared_ptr has the correct type. This lets
    // the derived class (this class) use its members in calls to
    // `std::bind`, without an ugly call to `dynamic_downcast` or
    // other nonsense.
    //
    : public std::enable_shared_from_this<sync_ws_con>

    // The stream should be created before the base class so
    // use the "base from member" idiom.
    //
    , public base_from_member<beast::websocket::stream<socket_type>>

    // Constructs last, destroys first
    //
    , public sync_ws_con_base<sync_ws_con>
{
public:
    // Construct the plain connection.
    //
    template<class... Args>
    explicit
    sync_ws_con(
        socket_type&& sock,
        Args&&... args)
        : base_from_member<beast::websocket::stream<socket_type>>(std::move(sock))
        , sync_ws_con_base<sync_ws_con>(std::forward<Args>(args)...)
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
    friend class sync_ws_con_base<sync_ws_con>;

    // This is called by the base before running the main loop.
    // There's nothing to do for a plain connection.
    //
    void
    do_handshake(error_code& ec)
    {
        // This is required by the specifications for error_code
        //
        ec = {};
    }
};

//------------------------------------------------------------------------------

/** A synchronous WebSocket @b PortHandler which implements echo.

    This is a port handler which accepts WebSocket upgrade HTTP
    requests and implements the echo protocol. All received
    WebSocket messages will be echoed back to the remote host.
*/
class ws_sync_port
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
    ws_sync_port(
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
    on_accept(socket_type&& sock, endpoint_type ep)
    {
        // Create our connection object and run it
        //
        std::make_shared<sync_ws_con>(
            std::move(sock),
            "ws_sync_port",
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
        // Create the connection object and run it,
        // transferring ownership of the ugprade request.
        //
        std::make_shared<sync_ws_con>(
            std::move(sock),
            "ws_sync_port",
            log_,
            instance_.next_id(),
            ep,
            cb_)->run(std::move(req));
    }
};

} // framework

#endif
