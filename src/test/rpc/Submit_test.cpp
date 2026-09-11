#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/pay.h>

#include <xrpld/core/Config.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/config/Constants.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/jss.h>

#include <memory>

namespace xrpl::test {

class Submit_test : public beast::unit_test::Suite
{
public:
    void
    testAugmentedFields()
    {
        testcase("Augmented fields in sign-and-submit mode");

        using namespace jtx;

        // Enable signing support in config
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    static std::string const kSigningSupportCfg =
                        std::string("[") + Sections::kSigningSupport + "]\ntrue";
                    cfg->loadFromString(kSigningSupportCfg);
                    return cfg;
                })};

        Account const alice{"alice"};
        Account const bob{"bob"};

        env.fund(XRP(10000), alice, bob);
        env.close();

        // Test 1: Sign-and-submit mode should return augmented fields
        {
            json::Value jv;
            jv[jss::tx_json][jss::TransactionType] = jss::Payment;
            jv[jss::tx_json][jss::Account] = alice.human();
            jv[jss::tx_json][jss::Destination] = bob.human();
            jv[jss::tx_json][jss::Amount] = XRP(100).value().getJson();
            jv[jss::secret] = alice.name();

            auto const result = env.rpc("json", "submit", to_string(jv))[jss::result];

            // These are the augmented fields that should be present
            BEAST_EXPECT(result.isMember(jss::engine_result));
            BEAST_EXPECT(result.isMember(jss::engine_result_code));
            BEAST_EXPECT(result.isMember(jss::engine_result_message));

            // New augmented fields from issue #3125
            BEAST_EXPECT(result.isMember(jss::accepted));
            BEAST_EXPECT(result.isMember(jss::applied));
            BEAST_EXPECT(result.isMember(jss::broadcast));
            BEAST_EXPECT(result.isMember(jss::queued));
            BEAST_EXPECT(result.isMember(jss::kept));

            // Current ledger state fields
            BEAST_EXPECT(result.isMember(jss::account_sequence_next));
            BEAST_EXPECT(result.isMember(jss::account_sequence_available));
            BEAST_EXPECT(result.isMember(jss::open_ledger_cost));
            BEAST_EXPECT(result.isMember(jss::validated_ledger_index));

            // Verify basic transaction fields
            BEAST_EXPECT(result.isMember(jss::tx_blob));
            BEAST_EXPECT(result.isMember(jss::tx_json));
        }

        // Test 2: Binary blob mode should also return augmented fields (regression test)
        {
            auto jt = env.jt(pay(alice, bob, XRP(100)));
            Serializer s;
            jt.stx->add(s);

            auto const result = env.rpc("submit", strHex(s.slice()))[jss::result];

            // Verify augmented fields are present in binary mode too
            BEAST_EXPECT(result.isMember(jss::engine_result));
            BEAST_EXPECT(result.isMember(jss::accepted));
            BEAST_EXPECT(result.isMember(jss::applied));
            BEAST_EXPECT(result.isMember(jss::broadcast));
            BEAST_EXPECT(result.isMember(jss::queued));
            BEAST_EXPECT(result.isMember(jss::kept));
            BEAST_EXPECT(result.isMember(jss::account_sequence_next));
            BEAST_EXPECT(result.isMember(jss::account_sequence_available));
            BEAST_EXPECT(result.isMember(jss::open_ledger_cost));
            BEAST_EXPECT(result.isMember(jss::validated_ledger_index));
        }
    }

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

                json::Value params;
                params[jss::tx_blob] = txBlob;
                params[jss::fail_hard] = param;
                auto const jrr = env.rpc("json", "submit", to_string(params))[jss::result];
                BEAST_EXPECT(jrr[jss::error] == "invalidParams");
                BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'fail_hard', not boolean.");
            }

            // Test with tx_json path (deprecated signing)
            {
                json::Value params;
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
        testInvalidFailHard(json::Value(json::ValueType::Object));
        testInvalidFailHard(json::Value(json::ValueType::Array));

        // Valid boolean values should work (not return invalidParams)
        {
            JTx const jt = env.jt(pay(alice, bob, XRP(1)));
            auto const txBlob = strHex(jt.stx->getSerializer().slice());

            json::Value params;
            params[jss::tx_blob] = txBlob;
            params[jss::fail_hard] = true;
            auto const jrr = env.rpc("json", "submit", to_string(params))[jss::result];
            BEAST_EXPECT(!jrr.isMember(jss::error) || jrr[jss::error] != "invalidParams");
        }
        {
            JTx const jt = env.jt(pay(alice, bob, XRP(1)));
            auto const txBlob = strHex(jt.stx->getSerializer().slice());

            json::Value params;
            params[jss::tx_blob] = txBlob;
            params[jss::fail_hard] = false;
            auto const jrr = env.rpc("json", "submit", to_string(params))[jss::result];
            BEAST_EXPECT(!jrr.isMember(jss::error) || jrr[jss::error] != "invalidParams");
        }
    }

    void
    run() override
    {
        testAugmentedFields();
        testFailHardValidation();
    }
};

BEAST_DEFINE_TESTSUITE(Submit, rpc, xrpl);

}  // namespace xrpl::test
