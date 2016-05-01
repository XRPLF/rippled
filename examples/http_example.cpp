//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

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

    using namespace beast::http;

    // Send HTTP request using beast
    request_v1<empty_body> req({"GET", "/", 11});
    req.headers.replace("Host", host + ":" + std::to_string(sock.remote_endpoint().port()));
    req.headers.replace("User-Agent", "Beast");
    prepare(req);
    write(sock, req);

    // Receive and print HTTP response using beast
    beast::streambuf sb;
    response_v1<streambuf_body> resp;
    read(sock, sb, resp);
    std::cout << resp;
}
