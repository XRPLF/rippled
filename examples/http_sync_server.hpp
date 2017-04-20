//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_EXAMPLE_HTTP_SYNC_SERVER_H_INCLUDED
#define BEAST_EXAMPLE_HTTP_SYNC_SERVER_H_INCLUDED

#include "file_body.hpp"
#include "mime_type.hpp"

#include <beast/http.hpp>
#include <beast/core/placeholders.hpp>
#include <beast/core/streambuf.hpp>
#include <boost/asio.hpp>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include <iostream>

namespace beast {
namespace http {

class http_sync_server
{
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

    using req_type = request<string_body>;
    using resp_type = response<file_body>;

    bool log_ = true;
    std::mutex m_;
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

    template<class... Args>
    void
    log(Args const&... args)
    {
        if(log_)
        {
            std::lock_guard<std::mutex> lock(m_);
            log_args(args...);
        }
    }

private:
    void
    log_args()
    {
    }

    template<class Arg, class... Args>
    void
    log_args(Arg const& arg, Args const&... args)
    {
        std::cerr << arg;
        log_args(args...);
    }

    void
    fail(error_code ec, std::string what)
    {
        log(what, ": ", ec.message(), "\n");
    }

    void
    fail(int id, error_code const& ec)
    {
        if(ec != boost::asio::error::operation_aborted &&
                ec != boost::asio::error::eof)
            log("#", id, " ", ec.message(), "\n");
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
        if(ec)
            return fail(ec, "accept");
        static int id_ = 0;
        std::thread{lambda{++id_, *this, std::move(sock_)}}.detach();
        acceptor_.async_accept(sock_,
            std::bind(&http_sync_server::on_accept, this,
                asio::placeholders::error));
    }

    void
    do_peer(int id, socket_type&& sock0)
    {
        socket_type sock(std::move(sock0));
        streambuf sb;
        error_code ec;
        for(;;)
        {
            req_type req;
            http::read(sock, sb, req, ec);
            if(ec)
                break;
            auto path = req.url;
            if(path == "/")
                path = "/index.html";
            path = root_ + path;
            if(! boost::filesystem::exists(path))
            {
                response<string_body> res;
                res.status = 404;
                res.reason = "Not Found";
                res.version = req.version;
                res.fields.insert("Server", "http_sync_server");
                res.fields.insert("Content-Type", "text/html");
                res.body = "The file '" + path + "' was not found";
                prepare(res);
                write(sock, res, ec);
                if(ec)
                    break;
                return;
            }
            try
            {
                resp_type res;
                res.status = 200;
                res.reason = "OK";
                res.version = req.version;
                res.fields.insert("Server", "http_sync_server");
                res.fields.insert("Content-Type", mime_type(path));
                res.body = path;
                prepare(res);
                write(sock, res, ec);
                if(ec)
                    break;
            }
            catch(std::exception const& e)
            {
                response<string_body> res;
                res.status = 500;
                res.reason = "Internal Error";
                res.version = req.version;
                res.fields.insert("Server", "http_sync_server");
                res.fields.insert("Content-Type", "text/html");
                res.body =
                    std::string{"An internal error occurred: "} + e.what();
                prepare(res);
                write(sock, res, ec);
                if(ec)
                    break;
            }
        }
        fail(id, ec);
    }
};

} // http
} // beast

#endif
