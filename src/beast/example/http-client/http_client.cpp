//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

//[example_http_client

#include <beast/core.hpp>
#include <beast/http.hpp>
#include <beast/version.hpp>
#include <boost/asio.hpp>
#include <cstdlib>
#include <iostream>
#include <string>

using tcp = boost::asio::ip::tcp; // from <boost/asio.hpp>
namespace http = beast::http; // from <beast/http.hpp>

int main()
{
    // A helper for reporting errors
    auto const fail =
        [](std::string what, beast::error_code ec)
        {
            std::cerr << what << ": " << ec.message() << std::endl;
            return EXIT_FAILURE;
        };

    beast::error_code ec;

    // Set up an asio socket
    boost::asio::io_service ios;
    tcp::resolver r{ios};
    tcp::socket sock{ios};

    // Look up the domain name
    std::string const host = "www.example.com";
    auto const lookup = r.resolve({host, "http"}, ec);
    if(ec)
        return fail("resolve", ec);

    // Make the connection on the IP address we get from a lookup
    boost::asio::connect(sock, lookup, ec);
    if(ec)
        return fail("connect", ec);

    // Set up an HTTP GET request message
    http::request<http::string_body> req{http::verb::get, "/", 11};
    req.set(http::field::host, host + ":" +
        std::to_string(sock.remote_endpoint().port()));
    req.set(http::field::user_agent, BEAST_VERSION_STRING);
    req.prepare_payload();

    // Write the HTTP request to the remote host
    http::write(sock, req, ec);
    if(ec)
        return fail("write", ec);

    // This buffer is used for reading and must be persisted
    beast::flat_buffer b;

    // Declare a container to hold the response
    http::response<http::dynamic_body> res;

    // Read the response
    http::read(sock, b, res, ec);
    if(ec)
        return fail("read", ec);

    // Write the message to standard out
    std::cout << res << std::endl;

    // Gracefully close the socket
    sock.shutdown(tcp::socket::shutdown_both, ec);

    // not_connected happens sometimes
    // so don't bother reporting it.
    //
    if(ec && ec != beast::errc::not_connected)
        return fail("shutdown", ec);

    // If we get here then the connection is closed gracefully
    return EXIT_SUCCESS;
}

//]
