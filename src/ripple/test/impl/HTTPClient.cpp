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
#include <ripple/test/HTTPClient.h>
#include <ripple/json/to_string.h>
#include <ripple/server/Port.h>
#include <beast/asio/streambuf.h>
#include <boost/asio.hpp>
#include <string>

namespace ripple {
namespace test {

class HTTPClient : public AbstractClient
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
        throw std::runtime_error("Missing HTTP port");
    }

    boost::asio::io_service ios_;
    boost::asio::ip::tcp::socket stream_;

public:
    explicit
    HTTPClient(Config const& cfg)
        : stream_(ios_)
    {
        stream_.connect(getEndpoint(cfg));
    }

    Json::Value
    rpc(std::string const& cmd,
        Json::Value const& params) override
    {
        std::string s;
        {
            Json::Value jr;
            jr["method"] = cmd;
            jr["params"] = params;
            s = to_string(jr);
        }
        
        using namespace boost::asio;
        using namespace std::string_literals;
        std::string r;
        r =
            "GET / HTTP/1.1\r\n"
            "Connection: Keep-Alive\r\n"s +
            "Content-Length: " + std::to_string(s.size()) +
            "\r\n";
        write(stream_, buffer(r));
        write(stream_, buffer(s));
        return {};
    }
};

std::unique_ptr<AbstractClient>
makeHTTPClient(Config const& cfg)
{
    return std::make_unique<HTTPClient>(cfg);
}

} // test
} // ripple
