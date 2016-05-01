//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include "http_stream.hpp"
#include "urls_large_data.hpp"

#include <boost/asio.hpp>
#include <iostream>

using namespace beast::http;
using namespace boost::asio;

template<class String>
void
err(error_code const& ec, String const& what)
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
            request_v1<empty_body> req({"GET", "/", 11});
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
