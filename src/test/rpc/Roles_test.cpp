//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#include <ripple/beast/unit_test.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>
#include <test/jtx/WSClient.h>

#include <boost/asio/ip/address_v4.hpp>

#include <string>
#include <unordered_map>

namespace ripple {

namespace test {

class Roles_test : public beast::unit_test::suite
{
    bool
    isValidIpAddress(std::string const& addr)
    {
        boost::system::error_code ec;
        boost::asio::ip::make_address(addr, ec);
        return !ec.failed();
    }

    void
    testRoles()
    {
        using namespace test::jtx;

        {
            Env env(*this);

            BEAST_EXPECT(env.rpc("ping")["result"]["role"] == "admin");
            BEAST_EXPECT(makeWSClient(env.app().config())
                             ->invoke("ping")["result"]["unlimited"]
                             .asBool());
        }
        {
            Env env{*this, envconfig(no_admin)};

            BEAST_EXPECT(!env.rpc("ping")["result"].isMember("role"));
            auto wsRes =
                makeWSClient(env.app().config())->invoke("ping")["result"];
            BEAST_EXPECT(
                !wsRes.isMember("unlimited") || !wsRes["unlimited"].asBool());
        }
        {
            Env env{*this, envconfig(secure_gateway)};

            BEAST_EXPECT(env.rpc("ping")["result"]["role"] == "proxied");
            auto wsRes =
                makeWSClient(env.app().config())->invoke("ping")["result"];
            BEAST_EXPECT(
                !wsRes.isMember("unlimited") || !wsRes["unlimited"].asBool());

            std::unordered_map<std::string, std::string> headers;
            Json::Value rpcRes;

            // IPv4 tests.
            headers["X-Forwarded-For"] = "12.34.56.78";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "proxied");
            BEAST_EXPECT(rpcRes["ip"] == "12.34.56.78");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            headers["X-Forwarded-For"] = "87.65.43.21, 44.33.22.11";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["ip"] == "87.65.43.21");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            headers["X-Forwarded-For"] = "87.65.43.21:47011, 44.33.22.11";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["ip"] == "87.65.43.21");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            headers = {};
            headers["Forwarded"] = "for=88.77.66.55";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["ip"] == "88.77.66.55");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            headers["Forwarded"] =
                "what=where;for=55.66.77.88;for=nobody;"
                "who=3";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["ip"] == "55.66.77.88");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            headers["Forwarded"] =
                "what=where; for=55.66.77.88, for=99.00.11.22;"
                "who=3";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["ip"] == "55.66.77.88");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            headers["Forwarded"] =
                "what=where; For=99.88.77.66, for=55.66.77.88;"
                "who=3";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["ip"] == "99.88.77.66");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            headers["Forwarded"] =
                "what=where; for=\"55.66.77.88:47011\";"
                "who=3";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["ip"] == "55.66.77.88");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            headers["Forwarded"] =
                "what=where; For= \" 99.88.77.66 \" ,for=11.22.33.44;"
                "who=3";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["ip"] == "99.88.77.66");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            wsRes = makeWSClient(env.app().config(), true, 2, headers)
                        ->invoke("ping")["result"];
            BEAST_EXPECT(
                !wsRes.isMember("unlimited") || !wsRes["unlimited"].asBool());

            std::string const name = "xrposhi";
            headers["X-User"] = name;
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "identified");
            BEAST_EXPECT(rpcRes["username"] == name);
            BEAST_EXPECT(rpcRes["ip"] == "99.88.77.66");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));
            wsRes = makeWSClient(env.app().config(), true, 2, headers)
                        ->invoke("ping")["result"];
            BEAST_EXPECT(wsRes["unlimited"].asBool());

            // IPv6 tests.
            headers = {};
            headers["X-Forwarded-For"] =
                "2001:db8:3333:4444:5555:6666:7777:8888";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "proxied");
            BEAST_EXPECT(
                rpcRes["ip"] == "2001:db8:3333:4444:5555:6666:7777:8888");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            headers["X-Forwarded-For"] =
                "2001:db8:3333:4444:5555:6666:7777:9999, a:b:c:d:e:f, "
                "g:h:i:j:k:l";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "proxied");
            BEAST_EXPECT(
                rpcRes["ip"] == "2001:db8:3333:4444:5555:6666:7777:9999");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            headers["X-Forwarded-For"] =
                "[2001:db8:3333:4444:5555:6666:7777:8888]";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "proxied");
            BEAST_EXPECT(
                rpcRes["ip"] == "2001:db8:3333:4444:5555:6666:7777:8888");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            headers["X-Forwarded-For"] =
                "[2001:db8:3333:4444:5555:6666:7777:9999], [a:b:c:d:e:f], "
                "[g:h:i:j:k:l]";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "proxied");
            BEAST_EXPECT(
                rpcRes["ip"] == "2001:db8:3333:4444:5555:6666:7777:9999");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            headers = {};
            headers["Forwarded"] =
                "for=\"[2001:db8:3333:4444:5555:6666:7777:aaaa]\"";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "proxied");
            BEAST_EXPECT(
                rpcRes["ip"] == "2001:db8:3333:4444:5555:6666:7777:aaaa");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            headers["Forwarded"] =
                "For=\"[2001:db8:bb:cc:dd:ee:ff::]:2345\", for=99.00.11.22";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "proxied");
            BEAST_EXPECT(rpcRes["ip"] == "2001:db8:bb:cc:dd:ee:ff::");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            headers["Forwarded"] =
                "proto=http;FOR=\"[2001:db8:11:22:33:44:55:66]\""
                ";by=203.0.113.43";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "proxied");
            BEAST_EXPECT(rpcRes["ip"] == "2001:db8:11:22:33:44:55:66");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            // IPv6 (dual) tests.
            headers = {};
            headers["X-Forwarded-For"] = "2001:db8:3333:4444:5555:6666:1.2.3.4";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "proxied");
            BEAST_EXPECT(
                rpcRes["ip"] == "2001:db8:3333:4444:5555:6666:1.2.3.4");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            headers["X-Forwarded-For"] =
                "2001:db8:3333:4444:5555:6666:5.6.7.8, a:b:c:d:e:f, "
                "g:h:i:j:k:l";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "proxied");
            BEAST_EXPECT(
                rpcRes["ip"] == "2001:db8:3333:4444:5555:6666:5.6.7.8");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            headers["X-Forwarded-For"] =
                "[2001:db8:3333:4444:5555:6666:9.10.11.12]";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "proxied");
            BEAST_EXPECT(
                rpcRes["ip"] == "2001:db8:3333:4444:5555:6666:9.10.11.12");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            headers["X-Forwarded-For"] =
                "[2001:db8:3333:4444:5555:6666:13.14.15.16], [a:b:c:d:e:f], "
                "[g:h:i:j:k:l]";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "proxied");
            BEAST_EXPECT(
                rpcRes["ip"] == "2001:db8:3333:4444:5555:6666:13.14.15.16");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            headers = {};
            headers["Forwarded"] =
                "for=\"[2001:db8:3333:4444:5555:6666:20.19.18.17]\"";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "proxied");
            BEAST_EXPECT(
                rpcRes["ip"] == "2001:db8:3333:4444:5555:6666:20.19.18.17");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            headers["Forwarded"] =
                "For=\"[2001:db8:bb:cc::24.23.22.21]\", for=99.00.11.22";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "proxied");
            BEAST_EXPECT(rpcRes["ip"] == "2001:db8:bb:cc::24.23.22.21");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));

            headers["Forwarded"] =
                "proto=http;FOR=\"[::11:22:33:44:45.55.65.75]:234\""
                ";by=203.0.113.43";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "proxied");
            BEAST_EXPECT(rpcRes["ip"] == "::11:22:33:44:45.55.65.75");
            BEAST_EXPECT(isValidIpAddress(rpcRes["ip"].asString()));
        }
    }

    void
    testInvalidIpAddresses()
    {
        using namespace test::jtx;

        {
            Env env(*this);

            std::unordered_map<std::string, std::string> headers;
            Json::Value rpcRes;

            // No "for=" in Forwarded.
            headers["Forwarded"] = "for 88.77.66.55";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "admin");
            BEAST_EXPECT(!rpcRes.isMember("ip"));

            headers["Forwarded"] = "by=88.77.66.55";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "admin");
            BEAST_EXPECT(!rpcRes.isMember("ip"));

            // Empty field.
            headers = {};
            headers["Forwarded"] = "for=";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "admin");
            BEAST_EXPECT(!rpcRes.isMember("ip"));

            headers = {};
            headers["X-Forwarded-For"] = "     ";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "admin");
            BEAST_EXPECT(!rpcRes.isMember("ip"));

            // Empty quotes.
            headers = {};
            headers["Forwarded"] = "for= \"    \" ";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "admin");
            BEAST_EXPECT(!rpcRes.isMember("ip"));

            headers = {};
            headers["X-Forwarded-For"] = "\"\"";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "admin");
            BEAST_EXPECT(!rpcRes.isMember("ip"));

            // Unbalanced outer quotes.
            headers = {};
            headers["X-Forwarded-For"] = "\"12.34.56.78   ";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "admin");
            BEAST_EXPECT(!rpcRes.isMember("ip"));

            headers["X-Forwarded-For"] = "12.34.56.78\"";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "admin");
            BEAST_EXPECT(!rpcRes.isMember("ip"));

            // Unbalanced square brackets for IPv6.
            headers = {};
            headers["Forwarded"] = "FOR=[2001:db8:bb:cc::";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "admin");
            BEAST_EXPECT(!rpcRes.isMember("ip"));

            headers = {};
            headers["X-Forwarded-For"] = "2001:db8:bb:cc::24.23.22.21]";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "admin");
            BEAST_EXPECT(!rpcRes.isMember("ip"));

            // Empty square brackets.
            headers = {};
            headers["Forwarded"] = "FOR=[]";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "admin");
            BEAST_EXPECT(!rpcRes.isMember("ip"));

            headers = {};
            headers["X-Forwarded-For"] = "\"  [      ]  \"";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "admin");
            BEAST_EXPECT(!rpcRes.isMember("ip"));
        }
    }

public:
    void
    run() override
    {
        testRoles();
        testInvalidIpAddresses();
    }
};

BEAST_DEFINE_TESTSUITE(Roles, app, ripple);

}  // namespace test

}  // namespace ripple
