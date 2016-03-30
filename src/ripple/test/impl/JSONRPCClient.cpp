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
#include <ripple/test/JSONRPCClient.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/to_string.h>
#include <ripple/server/Port.h>
#include <beast/asio/streambuf.h>
#include <beast/http/parser.h>
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

    boost::asio::io_service ios_;
    boost::asio::ip::tcp::socket stream_;
    boost::asio::streambuf bin_;
    beast::asio::streambuf bout_;

public:
    explicit
    JSONRPCClient(Config const& cfg)
        : stream_(ios_)
    {
        stream_.connect(getEndpoint(cfg));
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
        std::string s;
        {
            Json::Value jr;
            jr["method"] = cmd;
            if(params)
            {
                Json::Value& ja = jr["params"] = Json::arrayValue;
                ja.append(params);
            }
            s = to_string(jr);
        }

        using namespace boost::asio;
        using namespace std::string_literals;
        auto const r =
            "POST / HTTP/1.1\r\n"
            "Host: me\r\n"
            "Connection: Keep-Alive\r\n"s +
            "Content-Type: application/json; charset=UTF-8\r\n"s +
            "Content-Length: " + std::to_string(s.size()) + "\r\n"
            "\r\n" + s;
        write(stream_, buffer(r));

        read_until(stream_, bin_, "\r\n\r\n");
        beast::asio::streambuf body;
        beast::http::message m;
        beast::http::parser p(
            [&](void const* data, std::size_t size)
            {
                body.commit(buffer_copy(
                    body.prepare(size), const_buffer(data, size)));
            }, m, false);

        for(;;)
        {
            auto const result = p.write(bin_.data());
            if (result.first)
                Throw<boost::system::system_error>(result.first);

            bin_.consume(result.second);
            if(p.complete())
                break;
            bin_.commit(stream_.read_some(
                bin_.prepare(1024)));
        }

        Json::Reader jr;
        Json::Value jv;
        jr.parse(buffer_string(body.data()), jv);
        if(jv["result"].isMember("error"))
            jv["error"] = jv["result"]["error"];
        if(jv["result"].isMember("status"))
            jv["status"] = jv["result"]["status"];
        return jv;
    }
};

std::unique_ptr<AbstractClient>
makeJSONRPCClient(Config const& cfg)
{
    return std::make_unique<JSONRPCClient>(cfg);
}

} // test
} // ripple
