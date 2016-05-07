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

#ifndef BEAST_EXAMPLE_HTTP_ASYNC_SERVER_H_INCLUDED
#define BEAST_EXAMPLE_HTTP_ASYNC_SERVER_H_INCLUDED

#include "file_body.hpp"
#include "http_stream.hpp"

#include <beast/core/placeholders.hpp>
#include <boost/asio.hpp>
#include <cstdio>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace beast {
namespace http {

class http_async_server
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
    std::vector<std::thread> thread_;

public:
    http_async_server(endpoint_type const& ep,
            int threads, std::string const& root)
        : sock_(ios_)
        , acceptor_(ios_)
        , root_(root)
    {
        acceptor_.open(ep.protocol());
        acceptor_.bind(ep);
        acceptor_.listen(
            boost::asio::socket_base::max_connections);
        acceptor_.async_accept(sock_,
            std::bind(&http_async_server::on_accept, this,
                beast::asio::placeholders::error));
        thread_.reserve(threads);
        for(int i = 0; i < threads; ++i)
            thread_.emplace_back(
                [&] { ios_.run(); });
    }

    ~http_async_server()
    {
        error_code ec;
        ios_.dispatch(
            [&]{ acceptor_.close(ec); });
        for(auto& t : thread_)
            t.join();
    }

private:
    class peer : public std::enable_shared_from_this<peer>
    {
        int id_;
        stream<socket_type> stream_;
        boost::asio::io_service::strand strand_;
        std::string root_;
        req_type req_;

    public:
        peer(peer&&) = default;
        peer(peer const&) = default;
        peer& operator=(peer&&) = delete;
        peer& operator=(peer const&) = delete;

        explicit
        peer(socket_type&& sock, std::string const& root)
            : stream_(std::move(sock))
            , strand_(stream_.get_io_service())
            , root_(root)
        {
            static int n = 0;
            id_ = ++n;
        }

        void run()
        {
            do_read();
        }

        void do_read()
        {
            stream_.async_read(req_, strand_.wrap(
                std::bind(&peer::on_read, shared_from_this(),
                    asio::placeholders::error)));
        }

        void on_read(error_code ec)
        {
            if(ec)
                return fail(ec, "read");
            do_read();
            auto path = req_.url;
            if(path == "/")
                path = "/index.html";
            path = root_ + path;
            if(! boost::filesystem::exists(path))
            {
                response_v1<string_body> resp;
                resp.status = 404;
                resp.reason = "Not Found";
                resp.version = req_.version;
                resp.headers.replace("Server", "http_async_server");
                resp.body = "The file '" + path + "' was not found";
                prepare(resp);
                stream_.async_write(std::move(resp),
                    std::bind(&peer::on_write, shared_from_this(),
                        asio::placeholders::error));
                return;
            }
            resp_type resp;
            resp.status = 200;
            resp.reason = "OK";
            resp.version = req_.version;
            resp.headers.replace("Server", "http_async_server");
            resp.headers.replace("Content-Type", "text/html");
            resp.body = path;
            prepare(resp);
            stream_.async_write(std::move(resp),
                std::bind(&peer::on_write, shared_from_this(),
                    asio::placeholders::error));
        }

        void on_write(error_code ec)
        {
            if(ec)
                fail(ec, "write");
        }

    private:
        void
        fail(error_code ec, std::string what)
        {
            if(ec != boost::asio::error::operation_aborted)
            {
                std::cerr <<
                    "#" << std::to_string(id_) << " " <<
                        what << ": " << ec.message() << std::endl;
            }
        }
    };

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

    void
    on_accept(error_code ec)
    {
        if(! acceptor_.is_open())
            return;
        maybe_throw(ec, "accept");
        socket_type sock(std::move(sock_));
        acceptor_.async_accept(sock_,
            std::bind(&http_async_server::on_accept, this,
                asio::placeholders::error));
        std::make_shared<peer>(std::move(sock), root_)->run();
    }
};

} // http
} // beast

#endif
