//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "../common/root_certificates.hpp"

#include <beast/core.hpp>
#include <beast/http.hpp>
#include <beast/version.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <iostream>
#include <string>

using tcp = boost::asio::ip::tcp; // from <boost/asio.hpp>
namespace ssl = boost::asio::ssl; // from <boost/asio/ssl.hpp>
namespace http = beast::http; // from <beast/http.hpp>

int main()
{
    // A helper for reporting errors
    auto const fail =
        [](std::string what, beast::error_code ec)
        {
            std::cerr << what << ": " << ec.message() << std::endl;
            std::cerr.flush();
            return EXIT_FAILURE;
        };

    boost::system::error_code ec;

    // Normal boost::asio setup
    boost::asio::io_service ios;
    tcp::resolver r{ios};
    tcp::socket sock{ios};

    // Look up the domain name
    std::string const host = "www.example.com";
    auto const lookup = r.resolve({host, "https"}, ec);
    if(ec)
        return fail("resolve", ec);

    // Make the connection on the IP address we get from a lookup
    boost::asio::connect(sock, lookup, ec);
    if(ec)
        return fail("connect", ec);

    // Create the required ssl context
    ssl::context ctx{ssl::context::sslv23_client};
    
    // This holds the root certificate used for verification
    load_root_certificates(ctx, ec);
    if(ec)
        return fail("certificate", ec);

    // Wrap the now-connected socket in an SSL stream
    ssl::stream<tcp::socket&> stream{sock, ctx};
    stream.set_verify_mode(ssl::verify_peer | ssl::verify_fail_if_no_peer_cert);

    // Perform SSL handshaking
    stream.handshake(ssl::stream_base::client, ec);
    if(ec)
        return fail("handshake", ec);

    // Set up an HTTP GET request message
    http::request<http::string_body> req;
    req.method(http::verb::get);
    req.target("/");
    req.version = 11;
    req.set(http::field::host, host + ":" +
        std::to_string(sock.remote_endpoint().port()));
    req.set(http::field::user_agent, BEAST_VERSION_STRING);
    req.prepare_payload();

    // Write the HTTP request to the remote host
    http::write(stream, req, ec);
    if(ec)
        return fail("write", ec);

    // This buffer is used for reading and must be persisted
    beast::flat_buffer b;

    // Declare a container to hold the response
    http::response<http::dynamic_body> res;

    // Read the response
    http::read(stream, b, res, ec);
    if(ec)
        return fail("read", ec);

    // Write the message to standard out
    std::cout << res << std::endl;

    // Shut down SSL on the stream
    stream.shutdown(ec);
    if(ec && ec != boost::asio::error::eof)
        fail("ssl_shutdown ", ec);

    // If we get here then the connection is closed gracefully
    return EXIT_SUCCESS;
}
