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
#include <boost/asio.hpp>

namespace ripple {
namespace test {

class HTTPClient : public AbstractClient
{
    static
    boost::asio::ip::tcp::endpoint
    getEndpoint(BasicConfig const& cfg)
    {
#if 0
        auto const& names = config.section("server").values();
        result.reserve(names.size());
        for (auto const& name : cfg.section("server").values())
        {
            if (! cfg.exists(name))
                continue;
            ParsedPort parsed = common;
            parsed.name = name;
            parse_Port(parsed, config[name], log);
            result.push_back(to_Port(parsed, log));
        }
        auto iter = ;
        for (iter = setup.ports.cbegin();
                iter != setup.ports.cend(); ++iter)
            if (iter->protocol.count("http") > 0 ||
                    iter->protocol.count("https") > 0)
                break;
        if (iter == setup.ports.cend())
            return;
        setup.client.secure =
            iter->protocol.count("https") > 0;
        setup.client.ip = iter->ip.to_string();
        // VFALCO HACK! to make localhost work
        if (setup.client.ip == "0.0.0.0")
            setup.client.ip = "127.0.0.1";
        setup.client.port = iter->port;
        setup.client.user = iter->user;
        setup.client.password = iter->password;
        setup.client.admin_user = iter->admin_user;
        setup.client.admin_password = iter->admin_password;
#endif
    }

    //boost::asio::ip::tcp::socket sock_;

public:
    explicit
    HTTPClient(Config const& cfg)
    {

    }

    Json::Value
    rpc(Json::Value const& jv) override
    {
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
