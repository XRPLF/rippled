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
#include <string>
#include <unordered_map>

namespace ripple {

namespace test {

class Roles_test : public beast::unit_test::suite
{
    void
    testRoles()
    {
        using namespace test::jtx;

        {
            Env env(*this);

            BEAST_EXPECT(env.rpc("ping")["result"]["role"] == "admin");
            BEAST_EXPECT(makeWSClient(
                env.app().config())->invoke(
                "ping")["result"]["unlimited"].asBool());
        }
        {
            Env env { *this, envconfig(no_admin) };

            BEAST_EXPECT(! env.rpc("ping")["result"].isMember("role"));
            auto wsRes = makeWSClient(
                env.app().config())->invoke("ping")["result"];
            BEAST_EXPECT(
                !wsRes.isMember("unlimited") || !wsRes["unlimited"].asBool());
        }
        {
            Env env { *this, envconfig(secure_gateway) };

            BEAST_EXPECT(env.rpc("ping")["result"]["role"] == "proxied");
            auto wsRes = makeWSClient(
                env.app().config())->invoke("ping")["result"];
            BEAST_EXPECT(
                !wsRes.isMember("unlimited") || !wsRes["unlimited"].asBool());

            std::unordered_map<std::string, std::string> headers;
            headers["X-Forwarded-For"] = "12.34.56.78";
            auto rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "proxied");
            BEAST_EXPECT(rpcRes["ip"] == "12.34.56.78");

            headers["X-Forwarded-For"] = "87.65.43.21, 44.33.22.11";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["ip"] == "87.65.43.21");
            headers.erase("X-Forwarded-For");

            headers["Forwarded"] = "for=88.77.66.55";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["ip"] == "88.77.66.55");

            headers["Forwarded"] = "what=where;for=55.66.77.88;for=nobody;"
                "who=3";
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["ip"] == "55.66.77.88");

            wsRes = makeWSClient(
                env.app().config(), true, 2, headers)->invoke("ping")["result"];
            BEAST_EXPECT(
                !wsRes.isMember("unlimited") || !wsRes["unlimited"].asBool());

            std::string const name = "xrposhi";
            headers["X-User"] = name;
            rpcRes = env.rpc(headers, "ping")["result"];
            BEAST_EXPECT(rpcRes["role"] == "identified");
            BEAST_EXPECT(rpcRes["username"] == name);
            BEAST_EXPECT(rpcRes["ip"] == "55.66.77.88");
            wsRes = makeWSClient(
                env.app().config(), true, 2, headers)->invoke("ping")["result"];
            BEAST_EXPECT(wsRes["unlimited"].asBool());
        }
    }

public:
    void
    run() override
    {
        testRoles();
    }
};

BEAST_DEFINE_TESTSUITE(Roles, app, ripple);

}  // namespace test

}  // namespace ripple
