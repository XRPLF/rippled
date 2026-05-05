#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/PathSet.h>
#include <test/jtx/amount.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>

namespace xrpl {

class Discrepancy_test : public beast::unit_test::Suite
{
    // This is a legacy test ported from js/coffee. The ledger
    // state was originally setup via a saved ledger file and the relevant
    // entries have since been converted to the equivalent jtx/Env setup.
    // A payment with path and sendmax is made and the transaction is queried
    // to verify that the net of balance changes match the fee charged.
    void
    testXRPDiscrepancy(FeatureBitset features)
    {
        testcase("Discrepancy test : XRP Discrepancy");
        using namespace test::jtx;
        Env env{*this, features};

        Account const a1{"A1"};
        Account const a2{"A2"};
        Account const a3{"A3"};
        Account const a4{"A4"};
        Account const a5{"A5"};
        Account const a6{"A6"};
        Account const a7{"A7"};

        env.fund(XRP(2000), a1);
        env.fund(XRP(1000), a2, a6, a7);
        env.fund(XRP(5000), a3);
        env.fund(XRP(1000000), a4);
        env.fund(XRP(600000), a5);
        env.close();

        env(trust(a1, a3["CNY"](200000)));
        env(pay(a3, a1, a3["CNY"](31)));
        env.close();

        env(trust(a1, a2["JPY"](1000000)));
        env(pay(a2, a1, a2["JPY"](729117)));
        env.close();

        env(trust(a4, a2["JPY"](10000000)));
        env(pay(a2, a4, a2["JPY"](470056)));
        env.close();

        env(trust(a5, a3["CNY"](50000)));
        env(pay(a3, a5, a3["CNY"](8683)));
        env.close();

        env(trust(a6, a3["CNY"](3000)));
        env(pay(a3, a6, a3["CNY"](293)));
        env.close();

        env(trust(a7, a6["CNY"](50000)));
        env(pay(a6, a7, a6["CNY"](261)));
        env.close();

        env(offer(a4, XRP(49147), a2["JPY"](34501)));
        env(offer(a5, a3["CNY"](3150), XRP(80086)));
        env(offer(a7, XRP(1233), a6["CNY"](25)));
        env.close();

        test::PathSet const payPaths{
            test::TestPath{a2["JPY"], a2},
            test::TestPath{XRP, a2["JPY"], a2},
            test::TestPath{a6, XRP, a2["JPY"], a2}};

        env(pay(a1, a1, a2["JPY"](1000)),
            Json(payPaths.json()),
            Txflags(tfPartialPayment),
            Sendmax(a3["CNY"](56)));
        env.close();

        json::Value jrq2;
        jrq2[jss::binary] = false;
        jrq2[jss::transaction] = env.tx()->getJson(JsonOptions::KNone)[jss::hash];
        jrq2[jss::id] = 3;
        auto jrr = env.rpc("json", "tx", to_string(jrq2))[jss::result];
        uint64_t const fee{jrr[jss::Fee].asUInt()};
        auto meta = jrr[jss::meta];
        uint64_t sumPrev{0};
        uint64_t sumFinal{0};
        BEAST_EXPECT(meta[sfAffectedNodes.fieldName].size() == 9);
        for (auto const& an : meta[sfAffectedNodes.fieldName])
        {
            json::Value node;
            if (an.isMember(sfCreatedNode.fieldName))
            {
                node = an[sfCreatedNode.fieldName];
            }
            else if (an.isMember(sfModifiedNode.fieldName))
            {
                node = an[sfModifiedNode.fieldName];
            }
            else if (an.isMember(sfDeletedNode.fieldName))
            {
                node = an[sfDeletedNode.fieldName];
            }

            if (node && node[sfLedgerEntryType.fieldName] == jss::AccountRoot)
            {
                json::Value prevFields = node.isMember(sfPreviousFields.fieldName)
                    ? node[sfPreviousFields.fieldName]
                    : node[sfNewFields.fieldName];
                json::Value finalFields = node.isMember(sfFinalFields.fieldName)
                    ? node[sfFinalFields.fieldName]
                    : node[sfNewFields.fieldName];
                if (prevFields)
                {
                    sumPrev += beast::lexicalCastThrow<std::uint64_t>(
                        prevFields[sfBalance.fieldName].asString());
                }
                if (finalFields)
                {
                    sumFinal += beast::lexicalCastThrow<std::uint64_t>(
                        finalFields[sfBalance.fieldName].asString());
                }
            }
        }
        // the difference in balances (final and prev) should be the
        // fee charged
        BEAST_EXPECT(sumPrev - sumFinal == fee);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        auto const sa = testableAmendments();
        testXRPDiscrepancy(sa - featurePermissionedDEX);
        testXRPDiscrepancy(sa);
    }
};

BEAST_DEFINE_TESTSUITE(Discrepancy, app, xrpl);

}  // namespace xrpl
