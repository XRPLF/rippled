//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_EXAMPLE_SERVER_HTTP_SYNC_PORT_HPP
#define BEAST_EXAMPLE_SERVER_HTTP_SYNC_PORT_HPP

#include "server.hpp"

#include "http_base.hpp"
#include "service_list.hpp"

#include "../common/rfc7231.hpp"
#include "../common/write_msg.hpp"

#include <beast/core/flat_buffer.hpp>
#include <beast/core/handler_ptr.hpp>
#include <beast/http/dynamic_body.hpp>
#include <beast/http/parser.hpp>
#include <beast/http/read.hpp>
#include <beast/http/string_body.hpp>
#include <beast/http/write.hpp>
#include <memory>
#include <utility>
#include <ostream>
#include <thread>

namespace framework {

/** A synchronous HTTP connection.

    This base class implements an HTTP connection object using
    synchronous calls.

    It uses the Curiously Recurring Template pattern (CRTP) where
    we refer to the derived class in order to access the stream object
    to use for reading and writing. This lets the same class be used
    for plain and SSL stream objects.

    @tparam Services The list of services this connection will support.
*/
template<class Derived, class... Services>
class sync_http_con_base
    : public http_base
{
    // This function lets us access members of the derived class
    Derived&
    impl()
    {
        return static_cast<Derived&>(*this);
    }

    // The stream to use for logging
    std::ostream& log_;

    // The services configured for the port
    service_list<Services...> const& services_;

    // A small unique integer for logging
    std::size_t id_;

    // The remote endpoint. We cache it here because
    // calls to remote_endpoint() can fail / throw.
    //
    endpoint_type ep_;

    // The buffer for performing reads
    beast::flat_buffer buffer_;

public:
    /// Constructor
    sync_http_con_base(
        beast::string_view server_name,
        std::ostream& log,
        service_list<Services...> const& services,
        std::size_t id,
        endpoint_type const& ep)
        : http_base(server_name)
        , log_(log)
        , services_(services)
        , id_(id)
        , ep_(ep)

        // The buffer has a limit of 8192, otherwise
        // the server is vulnerable to a buffer attack.
        //
        , buffer_(8192)
    {
    }

    // This is called to start the connection after
    // it is accepted.
    //
    void
    run()
    {
        // Bind a shared pointer into the lambda for the
        // thread, so the sync_http_con_base is destroyed after
        // the thread function exits.
        //
        std::thread{
            &sync_http_con_base::do_run,
            impl().shared_from_this()
        }.detach();
    }

protected:
    // Called when a failure occurs
    //
    void
    fail(std::string what, error_code ec)
    {
        if(ec)
        {
            log_ <<
                "[#" << id_ << " " << ep_ << "] " <<
            what << ": " << ec.message() << std::endl;
        }
    }

private:
    // This lambda is passed to the service list to handle
    // the case of sending request objects of varying types.
    // In C++14 this is more easily accomplished using a generic
    // lambda, but we want C+11 compatibility so we manually
    // write the lambda out.
    //
    struct send_lambda
    {
        // holds "this"
        sync_http_con_base& self_;

        // holds the captured error code
        error_code& ec_;

    public:
        // Constructor
        //
        // Capture "this" and "ec"
        //
        send_lambda(sync_http_con_base& self, error_code& ec)
            : self_(self)
            , ec_(ec)
        {
        }

        // Sends a message
        //
        // Since this is a synchronous implementation we
        // just call the write function and block.
        //
        template<class Body, class Fields>
        void
        operator()(
            beast::http::response<Body, Fields>&& res) const
        {
            beast::http::serializer<false, Body, Fields> sr{res};
            beast::http::write(self_.impl().stream(), sr, ec_);
        }
    };

    void
    do_run()
    {
        error_code ec;

        // Give the derived class a chance to do stuff before we
        // enter the main loop. This is for SSL connections really.
        //
        impl().do_handshake(ec);

        if(ec)
            return fail("handshake", ec);

        // The main connection loop, we alternate between
        // reading a request and sending a response. On
        // error we log and return, which destroys the thread
        // and the stream (thus closing the connection)
        //
        for(;;)
        {
            // Arguments passed to the parser constructor are
            // forwarded to the message object. A single argument
            // is forwarded to the body constructor.
            //
            // We construct the dynamic body with a 1MB limit
            // to prevent vulnerability to buffer attacks.
            //
            beast::http::request_parser<beast::http::dynamic_body> parser(
                std::piecewise_construct, std::make_tuple(1024* 1024));

            // Read the header first
            beast::http::read_header(impl().stream(), buffer_, parser, ec);

            // This happens when the other end closes gracefully
            //
            if(ec == beast::http::error::end_of_stream)
            {
                // Give the derived class a chance to do stuff
                impl().do_shutdown(ec);
                if(ec && ec != beast::errc::not_connected)
                    return fail("shutdown", ec);
                return;
            }

            // Any other error and we fail the connection
            if(ec)
                return fail("read_header", ec);

            send_lambda send{*this, ec};

            auto& req = parser.get();

            // See if they are specifying Expect: 100-continue
            //
            if(rfc7231::is_expect_100_continue(req))
            {
                // They want to know if they should continue,
                // so send the appropriate response synchronously.
                //
                send(this->continue_100(req));

                // This happens when we send an HTTP message
                // whose semantics indicate that the connection
                // should be closed afterwards. For example if
                // we send a Connection: close.
                //
                if(ec == beast::http::error::end_of_stream)
                {
                    // Give the derived class a chance to do stuff
                    impl().do_shutdown(ec);
                    if(ec && ec != beast::errc::not_connected)
                        return fail("shutdown", ec);
                    return;
                }

                // Have to check the error every time we call the lambda
                //
                if(ec)
                    return fail("write", ec);
            }

            // Read the rest of the message, if any.
            //
            beast::http::read(impl().stream(), buffer_, parser, ec);

            // Shouldn't be getting end_of_stream here;
            // that would mean that we got an incomplete
            // message, counting as an error.
            //
            if(ec)
                return fail("read", ec);

            // Give each service a chance to handle the request
            //
            if(! services_.respond(
                std::move(impl().stream()),
                ep_,
                std::move(req),
                send))
            {
                // No service handled the request,
                // send a Bad Request result to the client.
                //
                send(this->bad_request(req));

                // This happens when we send an HTTP message
                // whose semantics indicate that the connection
                // should be closed afterwards. For example if
                // we send a Connection: close.
                //
                if(ec == beast::http::error::end_of_stream)
                {
                    // Give the derived class a chance to do stuff
                    impl().do_shutdown(ec);
                    if(ec && ec != beast::errc::not_connected)
                        return fail("shutdown", ec);
                    return;
                }

                // Have to check the error every time we call the lambda
                //
                if(ec)
                    return fail("write", ec);
            }
            else
            {
                // This happens when we send an HTTP message
                // whose semantics indicate that the connection
                // should be closed afterwards. For example if
                // we send a Connection: close.
                //
                if(ec == beast::http::error::end_of_stream)
                {
                    // Give the derived class a chance to do stuff
                    if(ec && ec != beast::errc::not_connected)
                        return fail("shutdown", ec);
                    return;
                }

                // Have to check the error every time we call the lambda
                //
                if(ec)
                    return fail("write", ec);

                // See if the service that handled the
                // response took ownership of the stream.
                if(! impl().stream().lowest_layer().is_open())
                {
                    // They took ownership so just return and
                    // let this sync_http_con_base object get destroyed.
                    return;
                }
            }

            // Theres no pipelining possible in a synchronous server
            // because we can't do reads and writes at the same time.
        }
    }
};

//------------------------------------------------------------------------------

// This class represents a synchronous HTTP connection which
// uses a plain TCP/IP socket (no encryption) as the stream.
//
template<class... Services>
class sync_http_con

    // Give this object the enable_shared_from_this, and have
    // the base class call impl().shared_from_this(). The reason
    // is so that the shared_ptr has the correct type. This lets
    // the derived class (this class) use its members in calls to
    // `std::bind`, without an ugly call to `dynamic_downcast` or
    // other nonsense.
    //
    : public std::enable_shared_from_this<sync_http_con<Services...>>

    // The stream should be created before the base class so
    // use the "base from member" idiom.
    //
    , public base_from_member<socket_type>

    // Constructs last, destroys first
    //
    , public sync_http_con_base<sync_http_con<Services...>, Services...>
{
public:
    // Constructor
    //
    // Additional arguments are forwarded to the base class
    //
    template<class... Args>
    sync_http_con(
        socket_type&& sock,
        Args&&... args)
        : base_from_member<socket_type>(std::move(sock))
        , sync_http_con_base<sync_http_con<Services...>, Services...>(
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
    socket_type&
    stream()
    {
        return this->member;
    }

private:
    // Base class needs to be a friend to call our private members
    friend class sync_http_con_base<sync_http_con<Services...>, Services...>;

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

    // This is called when the other end closes the connection gracefully.
    //
    void
    do_shutdown(error_code& ec)
    {
        stream().shutdown(socket_type::shutdown_both, ec);
    }
};

//------------------------------------------------------------------------------

/*  A synchronous HTTP port handler

    This type meets the requirements of @b PortHandler. It supports
    variable list of HTTP services in its template parameter list,
    and provides a synchronous connection implementation to service
*/
template<class... Services>
class http_sync_port
{
    server& instance_;
    std::ostream& log_;
    service_list<Services...> services_;

public:
    /** Constructor

        @param instance The server instance which owns this port

        @param log The stream to use for logging
    */
    http_sync_port(
        server& instance,
        std::ostream& log)
        : instance_(instance)
        , log_(log)
    {
    }

    /** Initialize a service

        Every service in the list must be initialized exactly once.

        @param ec Set to the error, if any occurred

        @param args Optional arguments forwarded to the service
        constructor.

        @tparam Index The 0-based index of the service to initialize.
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
        // Create a plain http connection object
        // and transfer ownership of the socket.
        //
        std::make_shared<sync_http_con<Services...>>(
            std::move(sock),
            "http_sync_port",
            log_,
            services_,
            instance_.next_id(),
            ep)->run();
    }
};

} // framework

#endif
