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

#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Sign.h>
#include <ripple/basics/base64.h>
#include <ripple/basics/strHex.h>
#include <test/jtx/envconfig.h>
#include <boost/asio.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/beast/http.hpp>
#include <boost/lexical_cast.hpp>
#include <thread>

namespace ripple {
namespace test {

class TrustedPublisherServer
{
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

    using req_type = boost::beast::http::request<boost::beast::http::string_body>;
    using resp_type = boost::beast::http::response<boost::beast::http::string_body>;
    using error_code = boost::system::error_code;

    socket_type sock_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::function<std::string(int)> getList_;

public:

    struct Validator
    {
        PublicKey masterPublic;
        PublicKey signingPublic;
        std::string manifest;
    };

    TrustedPublisherServer(
        boost::asio::io_context& ioc,
        std::pair<PublicKey, SecretKey> keys,
        std::string const& manifest,
        int sequence,
        NetClock::time_point expiration,
        int version,
        std::vector<Validator> const& validators)
            : sock_(ioc), acceptor_(ioc)
    {
        endpoint_type const& ep {
            beast::IP::Address::from_string (ripple::test::getEnvLocalhostAddr()),
            0}; // 0 means let OS pick the port based on what's available
        std::string data = "{\"sequence\":" + std::to_string(sequence) +
            ",\"expiration\":" +
            std::to_string(expiration.time_since_epoch().count()) +
            ",\"validators\":[";

        for (auto const& val : validators)
        {
            data += "{\"validation_public_key\":\"" + strHex(val.masterPublic) +
                "\",\"manifest\":\"" + val.manifest + "\"},";
        }
        data.pop_back();
        data += "]}";
        std::string blob = base64_encode(data);
        auto const sig = sign(keys.first, keys.second, makeSlice(data));
        getList_ = [blob, sig, manifest, version](int interval) {
            std::stringstream l;
            l << "{\"blob\":\"" << blob << "\"" <<
                ",\"signature\":\"" << strHex(sig) << "\"" <<
                ",\"manifest\":\"" << manifest << "\"" <<
                ",\"refresh_interval\": " << interval <<
                ",\"version\":" << version  << '}';
            return l.str();
        };

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
        boost::asio::executor_work_guard<boost::asio::executor> work;

        lambda(int id_, TrustedPublisherServer& self_, socket_type&& sock_)
            : id(id_)
            , self(self_)
            , sock(std::move(sock_))
            , work(sock_.get_executor())
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
        using namespace boost::beast;
        socket_type sock(std::move(sock0));
        multi_buffer sb;
        error_code ec;
        for (;;)
        {
            resp_type res;
            req_type req;
            try
            {
                http::read(sock, sb, req, ec);
                if (ec)
                    break;
                auto path = req.target().to_string();
                res.insert("Server", "TrustedPublisherServer");
                res.version(req.version());

                if (boost::starts_with(path, "/validators"))
                {
                    res.result(http::status::ok);
                    res.insert("Content-Type", "application/json");
                    if (path == "/validators/bad")
                        res.body() = "{ 'bad': \"1']" ;
                    else if (path == "/validators/missing")
                        res.body() = "{\"version\": 1}";
                    else
                    {
                        int refresh = 5;
                        if (boost::starts_with(path, "/validators/refresh"))
                            refresh =
                                boost::lexical_cast<unsigned int>(
                                    path.substr(20));
                        res.body() = getList_(refresh);
                    }
                }
                else if (boost::starts_with(path, "/sleep/"))
                {
                    auto const sleep_sec =
                        boost::lexical_cast<unsigned int>(path.substr(7));
                    std::this_thread::sleep_for(
                        std::chrono::seconds{sleep_sec});
                }
                else if (boost::starts_with(path, "/redirect"))
                {
                    if (boost::ends_with(path, "/301"))
                        res.result(http::status::moved_permanently);
                    else if (boost::ends_with(path, "/302"))
                        res.result(http::status::found);
                    else if (boost::ends_with(path, "/307"))
                        res.result(http::status::temporary_redirect);
                    else if (boost::ends_with(path, "/308"))
                        res.result(http::status::permanent_redirect);

                    std::stringstream location;
                    if (boost::starts_with(path, "/redirect_to/"))
                    {
                        location << path.substr(13);
                    }
                    else if (! boost::starts_with(path, "/redirect_nolo"))
                    {
                        location << "http://" << local_endpoint() <<
                            (boost::starts_with(path, "/redirect_forever/") ?
                                path : "/validators");
                    }
                    if (! location.str().empty())
                        res.insert("Location", location.str());
                }
                else
                {
                    // unknown request
                    res.result(boost::beast::http::status::not_found);
                    res.insert("Content-Type", "text/html");
                    res.body() = "The file '" + path + "' was not found";
                }

                res.prepare_payload();
            }
            catch (std::exception const& e)
            {
                res = {};
                res.result(boost::beast::http::status::internal_server_error);
                res.version(req.version());
                res.insert("Server", "TrustedPublisherServer");
                res.insert("Content-Type", "text/html");
                res.body() = std::string{"An internal error occurred"} + e.what();
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
