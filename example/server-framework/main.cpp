//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "server.hpp"

#include "http_async_port.hpp"
#include "http_sync_port.hpp"
#include "ws_async_port.hpp"
#include "ws_sync_port.hpp"

#if BEAST_USE_OPENSSL
#include "https_ports.hpp"
#include "multi_port.hpp"
#include "wss_ports.hpp"
#include "ssl_certificate.hpp"
#endif

#include "file_service.hpp"
#include "ws_upgrade_service.hpp"

#include <boost/program_options.hpp>

#include <iostream>

/// Block until SIGINT or SIGTERM is received.
void
sig_wait()
{
    // Create our own io_service for this
    boost::asio::io_service ios;

    // Get notified on the signals we want
    boost::asio::signal_set signals(
        ios, SIGINT, SIGTERM);

    // Now perform the asynchronous call
    signals.async_wait(
        [&](boost::system::error_code const&, int)
        {
        });

    // Block the current thread on run(), when the
    // signal is received then this call will return.
    ios.run();
}

/** Set the options on a WebSocket stream.

    This is used by the websocket server port handlers.
    It is called every time a new websocket stream is
    created, to provide the opportunity to set settings
    for the connection.
*/
class set_ws_options
{
    beast::websocket::permessage_deflate pmd_;

public:
    set_ws_options(beast::websocket::permessage_deflate const& pmd)
        : pmd_(pmd)
    {
    }

    template<class NextLayer>
    void
    operator()(beast::websocket::stream<NextLayer>& ws) const
    {
        ws.auto_fragment(false);
        ws.set_option(pmd_);
        ws.read_message_max(64 * 1024 * 1024);
    }
};

int
main(
    int ac,
    char const* av[])
{
    using namespace framework;
    using namespace beast::http;

    // Helper for reporting failures
    //
    auto const fail =
        [&](
            std::string const& what,
            error_code const& ec)
        {
            std::cerr <<
                av[0] << ": " <<
                what << " failed, " <<
                ec.message() <<
                std::endl;
            return EXIT_FAILURE;
        };

    namespace po = boost::program_options;
    po::options_description desc("Options");

    desc.add_options()
        ("root,r",      po::value<std::string>()->default_value("."),
                        "Set the root directory for serving files")
        ("port,p",      po::value<std::uint16_t>()->default_value(1000),
                        "Set the base port number for the server")
        ("ip",          po::value<std::string>()->default_value("0.0.0.0"),
                        "Set the IP address to bind to, \"0.0.0.0\" for all")
        ("threads,n",   po::value<std::size_t>()->default_value(4),
                        "Set the number of threads to use")
        ;
    po::variables_map vm;
    po::store(po::parse_command_line(ac, av, desc), vm);

    // Get the IP address from the options
    std::string const ip = vm["ip"].as<std::string>();
    
    // Get the port number from the options
    std::uint16_t const port = vm["port"].as<std::uint16_t>();

    // Build an endpoint from the address and port
    address_type const addr{address_type::from_string(ip)};

    // Get the number of threads from the command line
    std::size_t const threads = vm["threads"].as<std::size_t>();

    // Get the root path from the command line
    boost::filesystem::path const root =  vm["root"].as<std::string>();

    // These settings will be applied to all new websocket connections
    beast::websocket::permessage_deflate pmd;
    pmd.client_enable = true;
    pmd.server_enable = true;
    pmd.compLevel = 3;

    error_code ec;

    // Create our server instance with the specified number of threads
    server instance{threads};

    //--------------------------------------------------------------------------
    //
    // Synchronous  WebSocket   HTTP
    //
    //              port + 0    port + 1
    //
    //--------------------------------------------------------------------------
    {
        // Create a WebSocket port
        //
        auto wsp = instance.make_port<ws_sync_port>(
            ec,
            endpoint_type{addr,static_cast<unsigned short>(port + 0)},
            instance,
            std::cout,
            set_ws_options{pmd});

        if(ec)
            return fail("ws_sync_port", ec);

        // Create an HTTP port
        //
        auto sp = instance.make_port<http_sync_port<
                ws_upgrade_service<ws_sync_port>,
                file_service
            >>(
            ec,
            endpoint_type{addr,static_cast<unsigned short>(port + 1)},
            instance,
            std::cout);

        if(ec)
            return fail("http_sync_port", ec);

        // Init the ws_upgrade_service to
        // forward upgrades to the WebSocket port.
        //
        sp->template init<0>(
            ec,
            *wsp                // The WebSocket port handler
            );

        if(ec)
            return fail("http_sync_port/ws_upgrade_service", ec);

        // Init the file_service to point to the root path.
        //
        sp->template init<1>(
            ec,
            root,               // The root path
            "http_sync_port"    // The value for the Server field
            );

        if(ec)
            return fail("http_sync_port/file_service", ec);
    }

    //--------------------------------------------------------------------------
    //
    // Asynchronous WebSocket   HTTP
    //
    //              port + 2    port + 3
    //
    //--------------------------------------------------------------------------
    {
        // Create a WebSocket port
        //
        auto wsp = instance.make_port<ws_async_port>(
            ec,
            endpoint_type{addr,
                static_cast<unsigned short>(port + 2)},
            instance,
            std::cout,
            set_ws_options{pmd}
        );

        if(ec)
            return fail("ws_async_port", ec);

        // Create an HTTP port
        //
        auto sp = instance.make_port<http_async_port<
                ws_upgrade_service<ws_async_port>,
                file_service
            >>(
            ec,
            endpoint_type{addr,
                static_cast<unsigned short>(port + 3)},
            instance,
            std::cout);

        if(ec)
            return fail("http_async_port", ec);

        // Init the ws_upgrade_service to
        // forward upgrades to the WebSocket port.
        //
        sp->template init<0>(
            ec,
            *wsp                // The websocket port handler
            );

        if(ec)
            return fail("http_async_port/ws_upgrade_service", ec);

        // Init the file_service to point to the root path.
        //
        sp->template init<1>(
            ec,
            root,               // The root path
            "http_async_port"   // The value for the Server field
            );

        if(ec)
            return fail("http_async_port/file_service", ec);
    }

    //
    // The next section supports encrypted connections and requires
    // an installed and configured OpenSSL as part of the build.
    //

#if BEAST_USE_OPENSSL

    ssl_certificate cert;

    //--------------------------------------------------------------------------
    //
    // Synchronous  Secure WebSocket    HTTPS
    //
    //              port + 4            port + 5
    //
    //--------------------------------------------------------------------------
    {
        // Create a WebSocket port
        //
        auto wsp = instance.make_port<wss_sync_port>(
            ec,
            endpoint_type{addr,
                static_cast<unsigned short>(port + 4)},
            instance,
            std::cout,
            cert.get(),
            set_ws_options{pmd});

        if(ec)
            return fail("wss_sync_port", ec);

        // Create an HTTP port
        //
        auto sp = instance.make_port<https_sync_port<
                ws_upgrade_service<wss_sync_port>,
                file_service
            >>(
            ec,
            endpoint_type{addr,
                static_cast<unsigned short>(port + 5)},
            instance,
            std::cout,
            cert.get());

        if(ec)
            return fail("https_sync_port", ec);

        // Init the ws_upgrade_service to
        // forward upgrades to the WebSocket port.
        //
        sp->template init<0>(
            ec,
            *wsp                // The websocket port handler
            );

        if(ec)
            return fail("http_sync_port/ws_upgrade_service", ec);

        // Init the file_service to point to the root path.
        //
        sp->template init<1>(
            ec,
            root,               // The root path
            "http_sync_port"    // The value for the Server field
            );

        if(ec)
            return fail("https_sync_port/file_service", ec);
    }

    //--------------------------------------------------------------------------
    //
    // Asynchronous Secure WebSocket    HTTPS
    //
    //              port + 6            port + 7
    //
    //--------------------------------------------------------------------------
    {
        // Create a WebSocket port
        //
        auto wsp = instance.make_port<wss_async_port>(
            ec,
            endpoint_type{addr,
                static_cast<unsigned short>(port + 6)},
            instance,
            std::cout,
            cert.get(),
            set_ws_options{pmd}
        );

        if(ec)
            return fail("ws_async_port", ec);

        // Create an HTTP port
        //
        auto sp = instance.make_port<https_async_port<
                ws_upgrade_service<wss_async_port>,
                file_service
            >>(
            ec,
            endpoint_type{addr,
                static_cast<unsigned short>(port + 7)},
            instance,
            std::cout,
            cert.get());

        if(ec)
            return fail("https_async_port", ec);

        // Init the ws_upgrade_service to
        // forward upgrades to the WebSocket port.
        //
        sp->template init<0>(
            ec,
            *wsp                    // The websocket port handler
            );

        if(ec)
            return fail("https_async_port/ws_upgrade_service", ec);

        // Init the file_service to point to the root path.
        //
        sp->template init<1>(
            ec,
            root,                   // The root path
            "https_async_port"      // The value for the Server field
            );

        if(ec)
            return fail("https_async_port/file_service", ec);
    }

    //--------------------------------------------------------------------------
    //
    // Multi-Port   HTTP,   WebSockets,
    //              HTTPS   Secure WebSockets
    //
    //              Asynchronous, all on the same port!
    //
    //              port + 8
    //
    //--------------------------------------------------------------------------
    {
        // Create a multi_port
        //
        auto sp = instance.make_port<multi_port<
                ws_upgrade_service<multi_port_base>,
                file_service
            >>(
            ec,
            endpoint_type{addr,
                static_cast<unsigned short>(port + 8)},
            instance,
            std::cout,
            cert.get(),
            set_ws_options{pmd});

        if(ec)
            return fail("multi_port", ec);

        // Init the ws_upgrade_service to forward requests to the multi_port.
        //
        sp->template init<0>(
            ec,
            *sp                     // The websocket port handler
            );

        if(ec)
            return fail("multi_port/ws_upgrade_service", ec);

        // Init the ws_upgrade_service to
        // forward upgrades to the Multi port.
        //
        sp->template init<1>(
            ec,
            root,               // The root path
            "multi_port"        // The value for the Server field
            );

        if(ec)
            return fail("multi_port/file_service", ec);
    }

#endif

    sig_wait();
}
