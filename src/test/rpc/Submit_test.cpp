#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::test {

class Submit_test : public beast::unit_test::suite
{
public:
    void
    testFailHardValidation()
    {
        testcase("fail_hard parameter validation");
        using namespace jtx;
        Env env(*this);
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.close();

        // Lambda to test invalid fail_hard parameter types
        auto testInvalidFailHard = [&](auto const& param) {
            // Test with tx_blob path
            {
                JTx const jt = env.jt(pay(alice, bob, XRP(1)));
                auto const txBlob = strHex(jt.stx->getSerializer().slice());

                Json::Value params;
                params[jss::tx_blob] = txBlob;
                params[jss::fail_hard] = param;
                auto const jrr = env.rpc("json", "submit", to_string(params))[jss::result];
                BEAST_EXPECT(jrr[jss::error] == "invalidParams");
                BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'fail_hard', not boolean.");
            }

            // Test with tx_json path (deprecated signing)
            {
                Json::Value params;
                params[jss::secret] = toBase58(generateSeed("alice"));
                params[jss::tx_json] = pay("alice", "bob", XRP(1));
                params[jss::fail_hard] = param;
                auto const jrr = env.rpc("json", "submit", to_string(params))[jss::result];
                BEAST_EXPECT(jrr[jss::error] == "invalidParams");
                BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'fail_hard', not boolean.");
            }
        };

        // Test all invalid types
        testInvalidFailHard("true");
        testInvalidFailHard("yes");
        testInvalidFailHard(1);
        testInvalidFailHard(0);
        testInvalidFailHard(1.5);
        testInvalidFailHard(Json::Value(Json::objectValue));
        testInvalidFailHard(Json::Value(Json::arrayValue));

        // Valid boolean values should work (not return invalidParams)
        {
            JTx const jt = env.jt(pay(alice, bob, XRP(1)));
            auto const txBlob = strHex(jt.stx->getSerializer().slice());

            Json::Value params;
            params[jss::tx_blob] = txBlob;
            params[jss::fail_hard] = true;
            auto const jrr = env.rpc("json", "submit", to_string(params))[jss::result];
            BEAST_EXPECT(!jrr.isMember(jss::error) || jrr[jss::error] != "invalidParams");
        }
        {
            JTx const jt = env.jt(pay(alice, bob, XRP(1)));
            auto const txBlob = strHex(jt.stx->getSerializer().slice());

            Json::Value params;
            params[jss::tx_blob] = txBlob;
            params[jss::fail_hard] = false;
            auto const jrr = env.rpc("json", "submit", to_string(params))[jss::result];
            BEAST_EXPECT(!jrr.isMember(jss::error) || jrr[jss::error] != "invalidParams");
        }
    }

    void
    run() override
    {
        testFailHardValidation();
    }
};

BEAST_DEFINE_TESTSUITE(Submit, rpc, xrpl);

}  // namespace xrpl::test
