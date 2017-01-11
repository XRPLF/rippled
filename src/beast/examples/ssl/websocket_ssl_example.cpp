//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <beast/core/to_string.hpp>
#include <beast/websocket.hpp>
#include <beast/websocket/ssl.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
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
    std::string const host = "echo.websocket.org";
    io_service ios;
    resolver r{ios};
    socket sock{ios};
    connect(sock, r.resolve(resolver::query{host, "https"}));

    // Perform SSL handshaking
    using stream_type = ssl::stream<socket&>;
    ssl::context ctx{ssl::context::sslv23};
    stream_type stream{sock, ctx};
    stream.set_verify_mode(ssl::verify_none);
    stream.handshake(ssl::stream_base::client);

    // Secure WebSocket connect and send message using Beast
    beast::websocket::stream<stream_type&> ws{stream};
    ws.handshake(host, "/");
    ws.write(boost::asio::buffer("Hello, world!"));

    // Receive Secure WebSocket message, print and close using Beast
    beast::streambuf sb;
    beast::websocket::opcode op;
    ws.read(op, sb);
    ws.close(beast::websocket::close_code::normal);
    std::cout << to_string(sb.data()) << "\n";
}
