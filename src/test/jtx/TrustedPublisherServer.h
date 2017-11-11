//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2017 Ripple Labs Inc.

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
#ifndef RIPPLE_TEST_TRUSTED_PUBLISHER_SERVER_H_INCLUDED
#define RIPPLE_TEST_TRUSTED_PUBLISHER_SERVER_H_INCLUDED

#include <beast/core/detail/base64.hpp>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Sign.h>
#include <ripple/basics/strHex.h>
#include <boost/asio.hpp>
#include <beast/core/detail/base64.hpp>
#include <beast/http.hpp>

namespace ripple {
namespace test {

class TrustedPublisherServer
{
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

    using req_type = beast::http::request<beast::http::string_body>;
    using resp_type = beast::http::response<beast::http::string_body>;
    using error_code = boost::system::error_code;

    socket_type sock_;
    boost::asio::ip::tcp::acceptor acceptor_;

    std::string list_;

public:
    TrustedPublisherServer(
        endpoint_type const& ep,
        boost::asio::io_service& ios,
        std::pair<PublicKey, SecretKey> keys,
        std::string const& manifest,
        int sequence,
        NetClock::time_point expiration,
        int version,
        std::vector<PublicKey> const& validators)
        : sock_(ios), acceptor_(ios)
    {
        std::string data = "{\"sequence\":" + std::to_string(sequence) +
            ",\"expiration\":" +
            std::to_string(expiration.time_since_epoch().count()) +
            ",\"validators\":[";

        for (auto const& val : validators)
        {
            data += "{\"validation_public_key\":\"" + strHex(val) + "\"},";
        }
        data.pop_back();
        data += "]}";
        std::string blob = beast::detail::base64_encode(data);

        list_ = "{\"blob\":\"" + blob + "\"";

        auto const sig = sign(keys.first, keys.second, makeSlice(data));

        list_ += ",\"signature\":\"" + strHex(sig) + "\"";
        list_ += ",\"manifest\":\"" + manifest + "\"";
        list_ += ",\"version\":" + std::to_string(version) + '}';

        acceptor_.open(ep.protocol());
        error_code ec;
        acceptor_.set_option(
            boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
        acceptor_.bind(ep);
        acceptor_.listen(boost::asio::socket_base::max_connections);
        acceptor_.async_accept(
            sock_,
            std::bind(
                &TrustedPublisherServer::on_accept, this, std::placeholders::_1));
    }

    ~TrustedPublisherServer()
    {
        error_code ec;
        acceptor_.close(ec);
    }

    endpoint_type
    local_endpoint() const
    {
        return acceptor_.local_endpoint();
    }

private:
    struct lambda
    {
        int id;
        TrustedPublisherServer& self;
        socket_type sock;
        boost::asio::io_service::work work;

        lambda(int id_, TrustedPublisherServer& self_, socket_type&& sock_)
            : id(id_)
            , self(self_)
            , sock(std::move(sock_))
            , work(sock.get_io_service())
        {
        }

        void
        operator()()
        {
            self.do_peer(id, std::move(sock));
        }
    };

    void
    on_accept(error_code ec)
    {
        // ec must be checked before `acceptor_` or the member variable may be
        // accessed after the destructor has completed
        if (ec || !acceptor_.is_open())
            return;

        static int id_ = 0;
        std::thread{lambda{++id_, *this, std::move(sock_)}}.detach();
        acceptor_.async_accept(
            sock_,
            std::bind(
                &TrustedPublisherServer::on_accept, this, std::placeholders::_1));
    }

    void
    do_peer(int id, socket_type&& sock0)
    {
        socket_type sock(std::move(sock0));
        beast::multi_buffer sb;
        error_code ec;
        for (;;)
        {
            req_type req;
            beast::http::read(sock, sb, req, ec);
            if (ec)
                break;
            auto path = req.target().to_string();
            if (path != "/validators")
            {
                resp_type res;
                res.result(beast::http::status::not_found);
                res.version = req.version;
                res.insert("Server", "TrustedPublisherServer");
                res.insert("Content-Type", "text/html");
                res.body = "The file '" + path + "' was not found";
                res.prepare_payload();
                write(sock, res, ec);
                if (ec)
                    break;
            }
            resp_type res;
            res.result(beast::http::status::ok);
            res.version = req.version;
            res.insert("Server", "TrustedPublisherServer");
            res.insert("Content-Type", "application/json");

            res.body = list_;
            try
            {
                res.prepare_payload();
            }
            catch (std::exception const& e)
            {
                res = {};
                res.result(beast::http::status::internal_server_error);
                res.version = req.version;
                res.insert("Server", "TrustedPublisherServer");
                res.insert("Content-Type", "text/html");
                res.body = std::string{"An internal error occurred"} + e.what();
                res.prepare_payload();
            }
            write(sock, res, ec);
            if (ec)
                break;
        }
    }
};

}  // namespace test
}  // namespace ripple
#endif
