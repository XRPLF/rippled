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

#include <test/jtx.h>
#include <test/jtx/Env.h>
#include <test/jtx/PathSet.h>
#include <ripple/beast/unit_test.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <ripple/protocol/SField.h>

namespace ripple {

class Discrepancy_test : public beast::unit_test::suite
{
    // This is a legacy test ported from js/coffee. The ledger
    // state was originally setup via a saved ledger file and the relevant
    // entries have since been converted to the equivalent jtx/Env setup.
    // A payment with path and sendmax is made and the transaction is queried
    // to verify that the net of balance changes match the fee charged.
    void
    testXRPDiscrepancy (FeatureBitset features)
    {
        testcase ("Discrepancy test : XRP Discrepancy");
        using namespace test::jtx;
        Env env {*this, features};

        Account A1 {"A1"};
        Account A2 {"A2"};
        Account A3 {"A3"};
        Account A4 {"A4"};
        Account A5 {"A5"};
        Account A6 {"A6"};
        Account A7 {"A7"};

        env.fund(XRP(2000), A1);
        env.fund(XRP(1000), A2, A6, A7);
        env.fund(XRP(5000), A3);
        env.fund(XRP(1000000), A4);
        env.fund(XRP(600000), A5);
        env.close();

        env(trust(A1, A3["CNY"](200000)));
        env(pay(A3, A1, A3["CNY"](31)));
        env.close();

        env(trust(A1, A2["JPY"](1000000)));
        env(pay(A2, A1, A2["JPY"](729117)));
        env.close();

        env(trust(A4, A2["JPY"](10000000)));
        env(pay(A2, A4, A2["JPY"](470056)));
        env.close();

        env(trust(A5, A3["CNY"](50000)));
        env(pay(A3, A5, A3["CNY"](8683)));
        env.close();

        env(trust(A6, A3["CNY"](3000)));
        env(pay(A3, A6, A3["CNY"](293)));
        env.close();

        env(trust(A7, A6["CNY"](50000)));
        env(pay(A6, A7, A6["CNY"](261)));
        env.close();

        env(offer(A4, XRP(49147), A2["JPY"](34501)));
        env(offer(A5, A3["CNY"](3150), XRP(80086)));
        env(offer(A7, XRP(1233), A6["CNY"](25)));
        env.close();

        test::PathSet payPaths {
            test::Path {A2["JPY"], A2},
            test::Path {XRP, A2["JPY"], A2},
            test::Path {A6, XRP, A2["JPY"], A2} };

        env(pay(A1, A1, A2["JPY"](1000)),
            json(payPaths.json()),
            txflags(tfPartialPayment),
            sendmax(A3["CNY"](56)));
        env.close();

        Json::Value jrq2;
        jrq2[jss::binary] = false;
        jrq2[jss::transaction] = env.tx()->getJson(0)[jss::hash];
        jrq2[jss::id] = 3;
        auto jrr = env.rpc ("json", "tx", to_string(jrq2))[jss::result];
        uint64_t fee { jrr[jss::Fee].asUInt() };
        auto meta = jrr[jss::meta];
        uint64_t sumPrev {0};
        uint64_t sumFinal {0};
        BEAST_EXPECT(meta[sfAffectedNodes.fieldName].size() == 9);
        for(auto const& an : meta[sfAffectedNodes.fieldName])
        {
            Json::Value node;
            if(an.isMember(sfCreatedNode.fieldName))
                node = an[sfCreatedNode.fieldName];
            else if(an.isMember(sfModifiedNode.fieldName))
                node = an[sfModifiedNode.fieldName];
            else if(an.isMember(sfDeletedNode.fieldName))
                node = an[sfDeletedNode.fieldName];

            if(node && node[sfLedgerEntryType.fieldName] == "AccountRoot")
            {
                Json::Value prevFields =
                    node.isMember(sfPreviousFields.fieldName) ?
                    node[sfPreviousFields.fieldName] :
                    node[sfNewFields.fieldName];
                Json::Value finalFields =
                    node.isMember(sfFinalFields.fieldName) ?
                    node[sfFinalFields.fieldName] :
                    node[sfNewFields.fieldName];
                if(prevFields)
                    sumPrev += beast::lexicalCastThrow<std::uint64_t>(
                        prevFields[sfBalance.fieldName].asString());
                if(finalFields)
                    sumFinal += beast::lexicalCastThrow<std::uint64_t>(
                        finalFields[sfBalance.fieldName].asString());
            }
        }
        // the difference in balances (final and prev) should be the
        // fee charged
        BEAST_EXPECT(sumPrev-sumFinal == fee);
    }

public:
    void run () override
    {
        using namespace test::jtx;
        auto const sa = supported_amendments();
        testXRPDiscrepancy (sa - featureFlow - fix1373 - featureFlowCross);
        testXRPDiscrepancy (sa               - fix1373 - featureFlowCross);
        testXRPDiscrepancy (sa                         - featureFlowCross);
        testXRPDiscrepancy (sa);
    }
};

BEAST_DEFINE_TESTSUITE (Discrepancy, app, ripple);

} // ripple
