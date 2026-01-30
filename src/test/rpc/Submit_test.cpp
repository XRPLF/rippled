#include <test/jtx.h>

#include <xrpld/core/ConfigSections.h>

#include <xrpl/protocol/jss.h>

namespace xrpl {

class Submit_test : public beast::unit_test::suite
{
public:
    void
    testAugmentedFields()
    {
        testcase("Augmented fields in sign-and-submit mode");

        using namespace test::jtx;
        
        // Enable signing support in config
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->loadFromString("[" SECTION_SIGNING_SUPPORT "]\ntrue");
                    return cfg;
                })};
        
        Account const alice{"alice"};
        Account const bob{"bob"};
        
        env.fund(XRP(10000), alice, bob);
        env.close();

        // Test 1: Sign-and-submit mode should return augmented fields
        {
            Json::Value jv;
            jv[jss::tx_json][jss::TransactionType] = jss::Payment;
            jv[jss::tx_json][jss::Account] = alice.human();
            jv[jss::tx_json][jss::Destination] = bob.human();
            jv[jss::tx_json][jss::Amount] = XRP(100).value().getJson(JsonOptions::none);
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
    run() override
    {
        testAugmentedFields();
    }
};

BEAST_DEFINE_TESTSUITE(Submit, rpc, xrpl);

}  // namespace xrpl
