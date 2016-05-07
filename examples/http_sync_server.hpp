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

#ifndef BEAST_EXAMPLE_HTTP_SYNC_SERVER_H_INCLUDED
#define BEAST_EXAMPLE_HTTP_SYNC_SERVER_H_INCLUDED

#include "file_body.hpp"
#include "http_stream.hpp"

#include <boost/asio.hpp>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <utility>

#include <iostream>

namespace beast {
namespace http {

class http_sync_server
{
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

    using req_type = request_v1<string_body>;
    using resp_type = response_v1<file_body>;

    boost::asio::io_service ios_;
    socket_type sock_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::string root_;
    std::thread thread_;

public:
    http_sync_server(endpoint_type const& ep,
            std::string const& root)
        : sock_(ios_)
        , acceptor_(ios_)
        , root_(root)
    {
        acceptor_.open(ep.protocol());
        acceptor_.bind(ep);
        acceptor_.listen(
            boost::asio::socket_base::max_connections);
        acceptor_.async_accept(sock_,
            std::bind(&http_sync_server::on_accept, this,
                beast::asio::placeholders::error));
        thread_ = std::thread{[&]{ ios_.run(); }};
    }

    ~http_sync_server()
    {
        error_code ec;
        ios_.dispatch(
            [&]{ acceptor_.close(ec); });
        thread_.join();
    }

    void
    fail(error_code ec, std::string what)
    {
        std::cerr <<
            what << ": " << ec.message() << std::endl;
    }

    void
    maybe_throw(error_code ec, std::string what)
    {
        if(ec)
        {
            fail(ec, what);
            throw ec;
        }
    }

    struct lambda
    {
        int id;
        http_sync_server& self;
        socket_type sock;
        boost::asio::io_service::work work;

        lambda(int id_, http_sync_server& self_,
                socket_type&& sock_)
            : id(id_)
            , self(self_)
            , sock(std::move(sock_))
            , work(sock.get_io_service())
        {
        }

        void operator()()
        {
            self.do_peer(id, std::move(sock));
        }
    };

    void
    on_accept(error_code ec)
    {
        if(! acceptor_.is_open())
            return;
        maybe_throw(ec, "accept");
        static int id_ = 0;
        std::thread{lambda{++id_, *this, std::move(sock_)}}.detach();
        acceptor_.async_accept(sock_,
            std::bind(&http_sync_server::on_accept, this,
                asio::placeholders::error));
    }

    void
    fail(int id, error_code const& ec)
    {
        if(ec != boost::asio::error::operation_aborted &&
                ec != boost::asio::error::eof)
            std::cerr <<
                "#" << std::to_string(id) << " " << std::endl;
    }

    void
    do_peer(int id, socket_type&& sock)
    {
        http::stream<socket_type> hs(std::move(sock));
        error_code ec;
        for(;;)
        {
            req_type req;
            hs.read(req, ec);
            if(ec)
                break;
            auto path = req.url;
            if(path == "/")
                path = "/index.html";
            path = root_ + path;
            if(! boost::filesystem::exists(path))
            {
                response_v1<string_body> resp;
                resp.status = 404;
                resp.reason = "Not Found";
                resp.version = req.version;
                resp.headers.replace("Server", "http_sync_server");
                resp.body = "The file '" + path + "' was not found";
                prepare(resp);
                hs.write(resp, ec);
                if(ec)
                    break;
            }
            resp_type resp;
            resp.status = 200;
            resp.reason = "OK";
            resp.version = req.version;
            resp.headers.replace("Server", "http_sync_server");
            resp.headers.replace("Content-Type", "text/html");
            resp.body = path;
            prepare(resp);
            hs.write(resp, ec);
            if(ec)
                break;
        }
        fail(id, ec);
    }
};

} // http
} // beast

#endif
