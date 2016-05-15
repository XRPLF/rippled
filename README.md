# Beast

[![Build Status](https://travis-ci.org/vinniefalco/Beast.svg?branch=master)](https://travis-ci.org/vinniefalco/Beast) [![codecov]
(https://codecov.io/gh/vinniefalco/Beast/branch/master/graph/badge.svg)](https://codecov.io/gh/vinniefalco/Beast) [![Documentation]
(https://img.shields.io/badge/documentation-master-brightgreen.svg)](http://vinniefalco.github.io/beast/) [![License]
(https://img.shields.io/badge/license-boost-brightgreen.svg)](LICENSE_1_0.txt)

Beast provides implementations of the HTTP and WebSocket protocols
built on top of Boost.Asio and other parts of boost.

Requirements:

* Boost
* C++11 or greater
* OpenSSL (optional)

Example WebSocket program:
```C++
#include <beast/to_string.hpp>
#include <beast/websocket.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <string>

int main()
{
    // Normal boost::asio setup
    std::string const host = "echo.websocket.org";
    boost::asio::io_service ios;
    boost::asio::ip::tcp::resolver r(ios);
    boost::asio::ip::tcp::socket sock(ios);
    boost::asio::connect(sock,
        r.resolve(boost::asio::ip::tcp::resolver::query{host, "80"}));

    // WebSocket connect and send message using beast
    beast::websocket::stream<boost::asio::ip::tcp::socket&> ws(sock);
    ws.handshake(host, "/");
    ws.write(boost::asio::buffer("Hello, world!"));

    // Receive WebSocket message, print and close using beast
    beast::streambuf sb;
    beast::websocket::opcode op;
    ws.read(op, sb);
    ws.close(beast::websocket::close_code::normal);
    std::cout << to_string(sb.data()) << "\n";
}
```

Example HTTP program:
```C++
#include <beast/http.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <string>

int main()
{
    // Normal boost::asio setup
    std::string const host = "boost.org";
    boost::asio::io_service ios;
    boost::asio::ip::tcp::resolver r(ios);
    boost::asio::ip::tcp::socket sock(ios);
    boost::asio::connect(sock,
        r.resolve(boost::asio::ip::tcp::resolver::query{host, "http"}));

    // Send HTTP request using beast
    beast::http::request_v1<beast::http::empty_body> req;
    req.method = "GET";
    req.url = "/";
    req.version = 11;
    req.headers.replace("Host", host + ":" + std::to_string(sock.remote_endpoint().port()));
    req.headers.replace("User-Agent", "Beast");
    beast::http::prepare(req);
    beast::http::write(sock, req);

    // Receive and print HTTP response using beast
    beast::streambuf sb;
    beast::http::response_v1<beast::http::streambuf_body> resp;
    beast::http::read(sock, sb, resp);
    std::cout << resp;
}
```

Links:

* [Home](http://vinniefalco.github.io/)
* [Repository](https://github.com/vinniefalco/Beast)
* [Documentation](http://vinniefalco.github.io/beast/)
* [Autobahn.testsuite results](http://vinniefalco.github.io/autobahn/index.html)

Please report issues or questions here:
https://github.com/vinniefalco/Beast/issues
