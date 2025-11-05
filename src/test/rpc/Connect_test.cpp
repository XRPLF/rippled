#include <test/jtx.h>

#include <xrpl/protocol/jss.h>

namespace ripple {

class Connect_test : public beast::unit_test::suite
{
    void
    testErrors()
    {
        testcase("Errors");

        using namespace test::jtx;

        {
            // standalone mode should fail
            Env env{*this};
            BEAST_EXPECT(env.app().config().standalone());

            auto const result = env.rpc("json", "connect", "{}");
            BEAST_EXPECT(result[jss::result][jss::status] == "error");
            BEAST_EXPECT(result[jss::result].isMember(jss::error));
            BEAST_EXPECT(result[jss::result][jss::error] == "notSynced");
            BEAST_EXPECT(
                result[jss::result][jss::error_message] ==
                "Not synced to the network.");
        }
    }

public:
    void
    run() override
    {
        testErrors();
    }
};

BEAST_DEFINE_TESTSUITE(Connect, rpc, ripple);

}  // namespace ripple
