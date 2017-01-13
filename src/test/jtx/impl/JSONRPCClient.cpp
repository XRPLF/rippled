//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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
#include <BeastConfig.h>
#include <test/jtx/JSONRPCClient.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/server/Port.h>
#include <beast/http/message_v1.hpp>
#include <beast/http/streambuf_body.hpp>
#include <beast/http/string_body.hpp>
#include <beast/http/read.hpp>
#include <beast/http/write.hpp>
#include <boost/asio.hpp>
#include <string>

namespace ripple {
namespace test {

class JSONRPCClient : public AbstractClient
{
    static
    boost::asio::ip::tcp::endpoint
    getEndpoint(BasicConfig const& cfg)
    {
        auto& log = std::cerr;
        ParsedPort common;
        parse_Port (common, cfg["server"], log);
        for (auto const& name : cfg.section("server").values())
        {
            if (! cfg.exists(name))
                continue;
            ParsedPort pp;
            parse_Port(pp, cfg[name], log);
            if(pp.protocol.count("http") == 0)
                continue;
            using boost::asio::ip::address_v4;
            if(*pp.ip == address_v4{0x00000000})
                *pp.ip = address_v4{0x7f000001};
            return { *pp.ip, *pp.port };
        }
        Throw<std::runtime_error>("Missing HTTP port");
        return {}; // Silence compiler control paths return value warning
    }

    template <class ConstBufferSequence>
    static
    std::string
    buffer_string (ConstBufferSequence const& b)
    {
        using namespace boost::asio;
        std::string s;
        s.resize(buffer_size(b));
        buffer_copy(buffer(&s[0], s.size()), b);
        return s;
    }

    boost::asio::ip::tcp::endpoint ep_;
    boost::asio::io_service ios_;
    boost::asio::ip::tcp::socket stream_;
    beast::streambuf bin_;
    beast::streambuf bout_;
    unsigned rpc_version_;

public:
    explicit
    JSONRPCClient(Config const& cfg, unsigned rpc_version)
        : ep_(getEndpoint(cfg))
        , stream_(ios_)
        , rpc_version_(rpc_version)
    {   
        stream_.connect(ep_);
    }

    ~JSONRPCClient() override
    {
        //stream_.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
        //stream_.close();
    }

    /*
        Return value is an Object type with up to three keys:
            status
            error
            result
    */
    Json::Value
    invoke(std::string const& cmd,
        Json::Value const& params) override
    {
        using namespace beast::http;
        using namespace boost::asio;
        using namespace std::string_literals;

        request_v1<string_body> req;
        req.method = "POST";
        req.url = "/";
        req.version = 11;
        req.headers.insert("Content-Type", "application/json; charset=UTF-8");
        req.headers.insert("Host",
            ep_.address().to_string() + ":" + std::to_string(ep_.port()));
        {
            Json::Value jr;
            jr[jss::method] = cmd;
            if (rpc_version_ == 2)
            {
                jr[jss::jsonrpc] = "2.0";
                jr[jss::ripplerpc] = "2.0";
                jr[jss::id] = 5;
            }
            if(params)
            {
                Json::Value& ja = jr[jss::params] = Json::arrayValue;
                ja.append(params);
            }
            req.body = to_string(jr);
        }
        prepare(req);
        write(stream_, req);

        response_v1<streambuf_body> res;
        read(stream_, bin_, res);

        Json::Reader jr;
        Json::Value jv;
        jr.parse(buffer_string(res.body.data()), jv);
        if(jv["result"].isMember("error"))
            jv["error"] = jv["result"]["error"];
        if(jv["result"].isMember("status"))
            jv["status"] = jv["result"]["status"];
        return jv;
    }

    unsigned version() const override
    {
        return rpc_version_;
    }
};

std::unique_ptr<AbstractClient>
makeJSONRPCClient(Config const& cfg, unsigned rpc_version)
{
    return std::make_unique<JSONRPCClient>(cfg, rpc_version);
}

} // test
} // ripple
