//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "urls_large_data.hpp"

#include <beast/core/streambuf.hpp>
#include <beast/http.hpp>
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <iostream>

using namespace beast::http;
using namespace boost::asio;

template<class String>
void
err(beast::error_code const& ec, String const& what)
{
    std::cerr << what << ": " << ec.message() << std::endl;
}

int main(int, char const*[])
{
    io_service ios;
    for(auto const& host : urls_large_data())
    {
        try
        {
            ip::tcp::resolver r(ios);
            auto it = r.resolve(
                ip::tcp::resolver::query{host, "http"});
            ip::tcp::socket sock(ios);
            connect(sock, it);
            auto ep = sock.remote_endpoint();
            request<empty_body> req;
            req.method = "GET";
            req.url = "/";
            req.version = 11;
            req.fields.insert("Host", host + std::string(":") +
                boost::lexical_cast<std::string>(ep.port()));
            req.fields.insert("User-Agent", "beast/http");
            prepare(req);
            write(sock, req);
            response<string_body> res;
            streambuf sb;
            beast::http::read(sock, sb, res);
            std::cout << res;
        }
        catch(beast::system_error const& ec)
        {
            std::cerr << host << ": " << ec.what();
        }
        catch(...)
        {
            std::cerr << host << ": unknown exception" << std::endl;
        }
    }
}
