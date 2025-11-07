#include <test/jtx.h>
#include <test/jtx/WSClient.h>

#include "xrpl/beast/unit_test/suite.h"
#include "xrpl/protocol/jss.h"

namespace ripple {
namespace test {

class BookChanges_test : public beast::unit_test::suite
{
public:
    void
    testConventionalLedgerInputStrings()
    {
        testcase("Specify well-known strings as ledger input");
        jtx::Env env(*this);
        Json::Value params, resp;

        // As per convention in XRPL, ledgers can be specified with strings
        // "closed", "validated" or "current"
        params["ledger"] = "validated";
        resp = env.rpc("json", "book_changes", to_string(params));
        BEAST_EXPECT(!resp[jss::result].isMember(jss::error));
        BEAST_EXPECT(resp[jss::result][jss::status] == "success");
        BEAST_EXPECT(resp[jss::result][jss::validated] == true);

        params["ledger"] = "current";
        resp = env.rpc("json", "book_changes", to_string(params));
        BEAST_EXPECT(!resp[jss::result].isMember(jss::error));
        BEAST_EXPECT(resp[jss::result][jss::status] == "success");
        BEAST_EXPECT(resp[jss::result][jss::validated] == false);

        params["ledger"] = "closed";
        resp = env.rpc("json", "book_changes", to_string(params));
        BEAST_EXPECT(!resp[jss::result].isMember(jss::error));
        BEAST_EXPECT(resp[jss::result][jss::status] == "success");

        // In the unit-test framework, requesting for "closed" ledgers appears
        // to yield "validated" ledgers. This is not new behavior. It is also
        // observed in the unit tests for the LedgerHeader class.
        BEAST_EXPECT(resp[jss::result][jss::validated] == true);

        // non-conventional ledger input should throw an error
        params["ledger"] = "non_conventional_ledger_input";
        resp = env.rpc("json", "book_changes", to_string(params));
        BEAST_EXPECT(resp[jss::result].isMember(jss::error));
        BEAST_EXPECT(resp[jss::result][jss::status] != "success");
    }

    void
    testLedgerInputDefaultBehavior()
    {
        testcase(
            "If ledger_hash or ledger_index is not specified, the behavior "
            "must default to the `current` ledger");
        jtx::Env env(*this);

        // As per convention in XRPL, ledgers can be specified with strings
        // "closed", "validated" or "current"
        Json::Value const resp =
            env.rpc("json", "book_changes", to_string(Json::Value{}));
        BEAST_EXPECT(!resp[jss::result].isMember(jss::error));
        BEAST_EXPECT(resp[jss::result][jss::status] == "success");

        // I dislike asserting the below statement, because its dependent on the
        // unit-test framework BEAST_EXPECT(resp[jss::result][jss::ledger_index]
        // == 3);
    }

    void
    testDomainOffer()
    {
        testcase("Domain Offer");
        using namespace jtx;

        FeatureBitset const all{
            jtx::testable_amendments() | featurePermissionedDomains |
            featureCredentials | featurePermissionedDEX};

        Env env(*this, all);
        PermissionedDEX permDex(env);
        auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] =
            permDex;

        auto wsc = makeWSClient(env.app().config());

        env(offer(alice, XRP(10), USD(10)), domain(domainID));
        env.close();

        env(pay(bob, carol, USD(10)),
            path(~USD),
            sendmax(XRP(10)),
            domain(domainID));
        env.close();

        std::string const txHash{
            env.tx()->getJson(JsonOptions::none)[jss::hash].asString()};

        Json::Value const txResult = env.rpc("tx", txHash)[jss::result];
        auto const ledgerIndex = txResult[jss::ledger_index].asInt();

        Json::Value jvParams;
        jvParams[jss::ledger_index] = ledgerIndex;

        auto jv = wsc->invoke("book_changes", jvParams);
        auto jrr = jv[jss::result];

        BEAST_EXPECT(jrr[jss::changes].size() == 1);
        BEAST_EXPECT(
            jrr[jss::changes][0u][jss::domain].asString() ==
            to_string(domainID));
    }

    void
    run() override
    {
        testConventionalLedgerInputStrings();
        testLedgerInputDefaultBehavior();

        testDomainOffer();
        // Note: Other aspects of the book_changes rpc are fertile grounds
        // for unit-testing purposes. It can be included in future work
    }
};

BEAST_DEFINE_TESTSUITE(BookChanges, rpc, ripple);

}  // namespace test
}  // namespace ripple
