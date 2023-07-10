//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

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

#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/beast/unit_test.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

#include <boost/format.hpp>

namespace ripple {

namespace test {

namespace validator_data {
static auto const public_key =
    "nHBt9fsb4849WmZiCds4r5TXyBeQjqnH5kzPtqgMAQMgi39YZRPa";

static auto const token =
    "eyJ2YWxpZGF0aW9uX3NlY3JldF9rZXkiOiI5ZWQ0NWY4NjYyNDFjYzE4YTI3NDdiNT\n"
    "QzODdjMDYyNTkwNzk3MmY0ZTcxOTAyMzFmYWE5Mzc0NTdmYTlkYWY2IiwibWFuaWZl\n"
    "c3QiOiJKQUFBQUFGeEllMUZ0d21pbXZHdEgyaUNjTUpxQzlnVkZLaWxHZncxL3ZDeE\n"
    "hYWExwbGMyR25NaEFrRTFhZ3FYeEJ3RHdEYklENk9NU1l1TTBGREFscEFnTms4U0tG\n"
    "bjdNTzJmZGtjd1JRSWhBT25ndTlzQUtxWFlvdUorbDJWMFcrc0FPa1ZCK1pSUzZQU2\n"
    "hsSkFmVXNYZkFpQnNWSkdlc2FhZE9KYy9hQVpva1MxdnltR21WcmxIUEtXWDNZeXd1\n"
    "NmluOEhBU1FLUHVnQkQ2N2tNYVJGR3ZtcEFUSGxHS0pkdkRGbFdQWXk1QXFEZWRGdj\n"
    "VUSmEydzBpMjFlcTNNWXl3TFZKWm5GT3I3QzBrdzJBaVR6U0NqSXpkaXRROD0ifQ==\n";
}  // namespace validator_data

class ServerInfo_test : public beast::unit_test::suite
{
public:
    static std::unique_ptr<Config>
    makeValidatorConfig()
    {
        auto p = std::make_unique<Config>();
        boost::format toLoad(R"rippleConfig(
[validator_token]
%1%

[validators]
%2%

[port_grpc]
ip = 0.0.0.0
port = 50051

[port_admin]
ip = 0.0.0.0
port = 50052
protocol = wss2
admin = 127.0.0.1
)rippleConfig");

        p->loadFromString(boost::str(
            toLoad % validator_data::token % validator_data::public_key));

        setupConfigForUnitTests(*p);

        return p;
    }

    void
    testServerInfo()
    {
        using namespace test::jtx;

        {
            Env env(*this);
            auto const result = env.rpc("server_info");
            BEAST_EXPECT(!result[jss::result].isMember(jss::error));
            BEAST_EXPECT(result[jss::result][jss::status] == "success");
            BEAST_EXPECT(result[jss::result].isMember(jss::info));
        }

        {
            Env env(*this);

            // Call NetworkOPs directly and set the admin flag to false.
            // Expect that the admin ports are not included in the result.
            auto const result =
                env.app().getOPs().getServerInfo(true, false, 0);
            auto const& ports = result[jss::ports];
            BEAST_EXPECT(ports.isArray() && ports.size() == 0);
        }

        {
            auto config = makeValidatorConfig();
            auto const rpc_port =
                (*config)["port_rpc"].get<unsigned int>("port");
            auto const grpc_port =
                (*config)["port_grpc"].get<unsigned int>("port");
            auto const ws_port = (*config)["port_ws"].get<unsigned int>("port");
            BEAST_EXPECT(grpc_port);
            BEAST_EXPECT(rpc_port);
            BEAST_EXPECT(ws_port);

            Env env(*this, std::move(config));
            auto const result = env.rpc("server_info");
            BEAST_EXPECT(!result[jss::result].isMember(jss::error));
            BEAST_EXPECT(result[jss::result][jss::status] == "success");
            BEAST_EXPECT(result[jss::result].isMember(jss::info));
            BEAST_EXPECT(
                result[jss::result][jss::info][jss::pubkey_validator] ==
                validator_data::public_key);

            auto const& ports = result[jss::result][jss::info][jss::ports];
            BEAST_EXPECT(ports.isArray() && ports.size() == 3);
            for (auto const& port : ports)
            {
                auto const& proto = port[jss::protocol];
                BEAST_EXPECT(proto.isArray());
                auto const p = port[jss::port].asUInt();
                BEAST_EXPECT(p == rpc_port || p == ws_port || p == grpc_port);
                if (p == grpc_port)
                {
                    BEAST_EXPECT(proto.size() == 1);
                    BEAST_EXPECT(proto[0u].asString() == "grpc");
                }
                if (p == rpc_port)
                {
                    BEAST_EXPECT(proto.size() == 2);
                    BEAST_EXPECT(proto[0u].asString() == "http");
                    BEAST_EXPECT(proto[1u].asString() == "ws2");
                }
                if (p == ws_port)
                {
                    BEAST_EXPECT(proto.size() == 1);
                    BEAST_EXPECT(proto[0u].asString() == "ws");
                }
            }
        }
    }

    void
    run() override
    {
        testServerInfo();
    }
};

BEAST_DEFINE_TESTSUITE(ServerInfo, app, ripple);

}  // namespace test
}  // namespace ripple
