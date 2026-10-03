
#include <test/jtx/Env.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

#include <array>
#include <string>

namespace xrpl {

class Connect_test : public beast::unit_test::Suite
{
    // doConnect refuses every request in standalone mode before it reads a
    // parameter, so nothing below the standalone check is reachable from a
    // default Env. Leave standalone once the application is set up: connect is
    // a Condition::NoCondition command, so no other part of the dispatch path
    // reads this flag.
    static void
    leaveStandalone(test::jtx::Env& env)
    {
        env.app().config().setupControl(true, true, false);
    }

    void
    testStandalone()
    {
        testcase("Standalone");

        using namespace test::jtx;

        // standalone mode should fail
        Env env{*this};
        BEAST_EXPECT(env.app().config().standalone());

        auto const result = env.rpc("json", "connect", "{}");
        BEAST_EXPECT(result[jss::result][jss::status] == "error");
        BEAST_EXPECT(result[jss::result].isMember(jss::error));
        BEAST_EXPECT(result[jss::result][jss::error] == "notSynced");
        BEAST_EXPECT(result[jss::result][jss::error_message] == "Not synced to the network.");
    }

    void
    testMissingIp()
    {
        testcase("Missing ip");

        using namespace test::jtx;

        Env env{*this};
        leaveStandalone(env);

        auto const result = env.rpc("json", "connect", "{}")[jss::result];
        BEAST_EXPECT(result[jss::status] == "error");
        BEAST_EXPECT(result[jss::error] == "invalidParams");
        // missingFieldMessage, unlike missingFieldError, has no StaticString
        // overload, so jss::ip has to be converted explicitly.
        BEAST_EXPECT(result[jss::error_message] == RPC::missingFieldMessage(std::string(jss::ip)));
    }

    void
    testBadIp()
    {
        testcase("Invalid ip");

        using namespace test::jtx;

        Env env{*this};
        leaveStandalone(env);
        BEAST_EXPECT(!env.app().config().standalone());

        // Before the type check the array and the object returned a generic
        // internal error, and the scalars were stringified into "42",
        // "1.500000", "true" and "", none of which parse as an address. The
        // connection was silently skipped, but the reply still claimed one had
        // been attempted.
        std::array<char const*, 6> const badIps{
            R"({"ip": 42})",
            R"({"ip": 1.5})",
            R"({"ip": true})",
            R"({"ip": null})",
            R"({"ip": {"host": "127.0.0.1"}})",
            R"({"ip": ["127.0.0.1"]})",
        };

        for (auto const* badIp : badIps)
        {
            auto const result = env.rpc("json", "connect", badIp)[jss::result];
            BEAST_EXPECTS(result[jss::status] == "error", badIp);
            BEAST_EXPECTS(result[jss::error] == "invalidParams", badIp);
            BEAST_EXPECTS(
                result[jss::error_message] == RPC::expectedFieldMessage(jss::ip, "a string"),
                badIp);
        }
    }

public:
    void
    run() override
    {
        testStandalone();
        testMissingIp();
        testBadIp();
    }
};

BEAST_DEFINE_TESTSUITE(Connect, rpc, xrpl);

}  // namespace xrpl
