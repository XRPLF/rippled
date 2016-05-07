//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "http_stream.hpp"
#include "urls_large_data.hpp"

#include <boost/asio.hpp>
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
            stream<ip::tcp::socket> hs(ios);
            connect(hs.lowest_layer(), it);
            auto ep = hs.lowest_layer().remote_endpoint();
            request_v1<empty_body> req;
            req.method = "GET";
            req.url = "/";
            req.version = 11;
            req.headers.insert("Host", host +
                std::string(":") + std::to_string(ep.port()));
            req.headers.insert("User-Agent", "beast/http");
            prepare(req);
            hs.write(req);
            response_v1<string_body> resp;
            hs.read(resp);
            std::cout << resp;
        }
        catch(boost::system::system_error const& ec)
        {
            std::cerr << host << ": " << ec.what();
        }
        catch(...)
        {
            std::cerr << host << ": unknown exception" << std::endl;
        }
    }
}
