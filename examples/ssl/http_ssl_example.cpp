//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <beast/http.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/lexical_cast.hpp>
#include <iostream>
#include <string>

int main()
{
    using boost::asio::connect;
    using socket = boost::asio::ip::tcp::socket;
    using resolver = boost::asio::ip::tcp::resolver;
    using io_service = boost::asio::io_service;
    namespace ssl = boost::asio::ssl;

    // Normal boost::asio setup
    std::string const host = "github.com";
    io_service ios;
    resolver r{ios};
    socket sock{ios};
    connect(sock, r.resolve(resolver::query{host, "https"}));

    // Perform SSL handshaking
    ssl::context ctx{ssl::context::sslv23};
    ssl::stream<socket&> stream{sock, ctx};
    stream.set_verify_mode(ssl::verify_none);
    stream.handshake(ssl::stream_base::client);

    // Send HTTP request over SSL using Beast
    beast::http::request<beast::http::empty_body> req;
    req.method = "GET";
    req.url = "/";
    req.version = 11;
    req.fields.insert("Host", host + ":" +
        boost::lexical_cast<std::string>(sock.remote_endpoint().port()));
    req.fields.insert("User-Agent", "Beast");
    beast::http::prepare(req);
    beast::http::write(stream, req);

    // Receive and print HTTP response using Beast
    beast::streambuf sb;
    beast::http::response<beast::http::streambuf_body> resp;
    beast::http::read(stream, sb, resp);
    std::cout << resp;

    // Shut down SSL on the stream
    boost::system::error_code ec;
    stream.shutdown(ec);
    if(ec && ec != boost::asio::error::eof)
        std::cout << "error: " << ec.message();
}
