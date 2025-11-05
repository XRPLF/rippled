#include <test/jtx.h>

#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/core/ConfigSections.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/jss.h>

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
        testcase("server_info");

        using namespace test::jtx;

        {
            Env env(*this);
            auto const serverinfo = env.rpc("server_info");
            BEAST_EXPECT(serverinfo.isMember(jss::result));
            auto const& result = serverinfo[jss::result];
            BEAST_EXPECT(!result.isMember(jss::error));
            BEAST_EXPECT(result[jss::status] == "success");
            BEAST_EXPECT(result.isMember(jss::info));
            auto const& info = result[jss::info];
            BEAST_EXPECT(info.isMember(jss::build_version));
            // Git info is not guaranteed to be present
            if (info.isMember(jss::git))
            {
                auto const& git = info[jss::git];
                BEAST_EXPECT(
                    git.isMember(jss::hash) || git.isMember(jss::branch));
                BEAST_EXPECT(
                    !git.isMember(jss::hash) ||
                    (git[jss::hash].isString() &&
                     git[jss::hash].asString().size() == 40));
                BEAST_EXPECT(
                    !git.isMember(jss::branch) ||
                    (git[jss::branch].isString() &&
                     git[jss::branch].asString().size() != 0));
            }
        }

        {
            Env env(*this);

            // Call NetworkOPs directly and set the admin flag to false.
            auto const result =
                env.app().getOPs().getServerInfo(true, false, 0);
            // Expect that the admin ports are not included in the result.
            auto const& ports = result[jss::ports];
            BEAST_EXPECT(ports.isArray() && ports.size() == 0);
            // Expect that git info is absent
            BEAST_EXPECT(!result.isMember(jss::git));
        }

        {
            Env env(*this, makeValidatorConfig());
            auto const& config = env.app().config();

            auto const rpc_port = config["port_rpc"].get<unsigned int>("port");
            auto const grpc_port =
                config[SECTION_PORT_GRPC].get<unsigned int>("port");
            auto const ws_port = config["port_ws"].get<unsigned int>("port");
            BEAST_EXPECT(grpc_port);
            BEAST_EXPECT(rpc_port);
            BEAST_EXPECT(ws_port);

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

BEAST_DEFINE_TESTSUITE(ServerInfo, rpc, ripple);

}  // namespace test
}  // namespace ripple
