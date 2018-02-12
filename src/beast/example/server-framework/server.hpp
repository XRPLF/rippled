//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_EXAMPLE_FRAMEWORK_SERVER_HPP
#define BEAST_EXAMPLE_FRAMEWORK_SERVER_HPP

#include "framework.hpp"

#include <boost/optional.hpp>
#include <memory>
#include <string>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace framework {

/** A server instance that accepts TCP/IP connections.

    This is a general purpose TCP/IP server which contains
    zero or more user defined "ports". Each port represents
    a listening socket whose behavior is defined by an
    instance of the @b PortHandler concept.

    To use the server, construct the class and then add the
    ports that you want using @ref make_port.

    @par Example

    @code

    // Create a server with 4 threads 
    //
    framework::server si(4);

    // Create a port that echoes everything back.
    // Bind all available interfaces on port 1000.
    //
    framework::error_code ec;
    si.make_port<echo_port>(
        ec,
        server::endpoint_type{
            server::address_type::from_string("0.0.0.0"), 1000}
    );

    ...

    // Close all connections, shut down the server
    si.stop();

    @endcode
*/
class server
{
    io_service_type ios_;
    std::vector<std::thread> tv_;
    boost::optional<boost::asio::io_service::work> work_;

public:
    server(server const&) = delete;
    server& operator=(server const&) = delete;

    /** Constructor

        @param n The number of threads to run on the `io_service`,
        which must be greater than zero.
    */
    explicit
    server(std::size_t n = 1)
        : work_(ios_)
    {
        if(n < 1)
            throw std::invalid_argument{"threads < 1"};
        tv_.reserve(n);
        while(n--)
            tv_.emplace_back(
                [&]
                {
                    ios_.run();
                });
    }

    /** Destructor

        Upon destruction, the `io_service` will be stopped
        and all pending completion handlers destroyed.
    */
    ~server()
    {
        work_ = boost::none;
        ios_.stop();
        for(auto& t : tv_)
            t.join();
    }

    /// Return the `io_service` associated with the server
    boost::asio::io_service&
    get_io_service()
    {
        return ios_;
    }

    /** Return a new, small integer unique id

        These ids are used to uniquely identify connections
        in log output.
    */
    std::size_t
    next_id()
    {
        static std::atomic<std::size_t> id_{0};
        return ++id_;
    }

    /** Create a listening port.

        @param ec Set to the error, if any occurred.

        @param ep The address and port to bind to.

        @param args Optional arguments, forwarded to the
        port handler's constructor.

        @tparam PortHandler The port handler to use for handling
        incoming connections on this port. This handler must meet
        the requirements of @b PortHandler. A model of PortHandler
        is as follows:

        @code

        struct PortHandler
        {
            void
            on_accept(
                endpoint_type ep,       // address of the remote endpoint
                socket_type&& sock,     // the connected socket
            );
        };

        @endcode
    */
    template<class PortHandler, class... Args>
    std::shared_ptr<PortHandler>
    make_port(
        error_code& ec,
        endpoint_type const& ep,
        Args&&... args);
};

//------------------------------------------------------------------------------

/*  This implementation class wraps the PortHandler and
    manages the listening socket. Upon an incoming connection
    it transfers ownership of the socket to the PortHandler.
*/
template<class PortHandler>
class port
    : public std::enable_shared_from_this<
        port<PortHandler>>
{
    server& instance_;
    PortHandler handler_;
    endpoint_type ep_;
    strand_type strand_;
    acceptor_type acceptor_;
    socket_type sock_;

public:
    // Constructor
    //
    // args are forwarded to the PortHandler
    //
    template<class... Args>
    explicit
    port(server& instance, Args&&... args)
        : instance_(instance)
        , handler_(std::forward<Args>(args)...)
        , strand_(instance.get_io_service())
        , acceptor_(instance.get_io_service())
        , sock_(instance.get_io_service())
    {
    }

    // Return the PortHandler wrapped in a shared_ptr
    //
    std::shared_ptr<PortHandler>
    handler()
    {
        // This uses a feature of std::shared_ptr invented by
        // Peter Dimov where the managed object piggy backs off
        // the reference count of another object containing it.
        //
        return std::shared_ptr<PortHandler>(
            this->shared_from_this(), &handler_);
    }

    // Open the listening socket
    //
    void
    open(endpoint_type const& ep, error_code& ec)
    {
        acceptor_.open(ep.protocol(), ec);
        if(ec)
            return;
        acceptor_.set_option(
            boost::asio::socket_base::reuse_address{true});
        acceptor_.bind(ep, ec);
        if(ec)
            return;
        acceptor_.listen(
            boost::asio::socket_base::max_connections, ec);
        if(ec)
            return;
        acceptor_.async_accept(sock_, ep_,
            std::bind(&port::on_accept, this->shared_from_this(),
                std::placeholders::_1));
    }

private:
    // Called when an incoming connection is accepted
    //
    void
    on_accept(error_code ec)
    {
        if(! acceptor_.is_open())
            return;
        if(ec == boost::asio::error::operation_aborted)
            return;
        if(! ec)
        {
            // Transfer ownership of the socket to the PortHandler
            //
            handler_.on_accept(std::move(sock_), ep_);
        }
        acceptor_.async_accept(sock_, ep_,
            std::bind(&port::on_accept, this->shared_from_this(),
                std::placeholders::_1));
    }
};

//------------------------------------------------------------------------------

template<class PortHandler, class... Args>
std::shared_ptr<PortHandler>
server::
make_port(
    error_code& ec,
    endpoint_type const& ep,
    Args&&... args)
{
    auto sp = std::make_shared<port<PortHandler>>(
        *this, std::forward<Args>(args)...);
    sp->open(ep, ec);
    if(ec)
        return nullptr;
    return sp->handler();
}

} // framework

#endif
