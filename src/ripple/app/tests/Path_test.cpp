//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2015 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/test/jtx.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/rpc/RipplePathFind.h>
#include <ripple/basics/Log.h>
#include <beast/unit_test/suite.h>

namespace ripple {
namespace test {

using namespace jtx;

class Path_test : public beast::unit_test::suite
{
public:
    Json::Value
    findPath (std::shared_ptr<ReadView const> const& view,
        Account const& src, Account const& dest,
            std::vector<Issue> const& srcIssues,
                STAmount const& saDstAmount)
    {
        auto jvSrcCurrencies = Json::Value(Json::arrayValue);
        for (auto const& i : srcIssues)
        {
            STAmount const a = STAmount(i, 0);
            jvSrcCurrencies.append(a.getJson(0));
        }

        int const level = 8;
        auto result = ripplePathFind(
            std::make_shared<RippleLineCache>(view),
                src.id(), dest.id(), saDstAmount,
                    jvSrcCurrencies, boost::none, level);
        if(!result.first)
            throw std::runtime_error(
            "Path_test::findPath: ripplePathFind find failed");

        return result.second;
    }

    void
    test_no_direct_path_no_intermediary_no_alternatives()
    {
        testcase("no direct path no intermediary no alternatives");
        Env env(*this);
        env.fund(XRP(10000), "alice", "bob");

        auto const alternatives = findPath(env.open(), "alice", "bob",
            {Account("alice")["USD"]}, Account("bob")["USD"](5));
        expect(alternatives.size() == 0);
    }

    void
    test_direct_path_no_intermediary()
    {
        testcase("direct path no intermediary");
        Env env(*this);
        env.fund(XRP(10000), "alice", "bob");
        env.trust(Account("alice")["USD"](700), "bob");

        auto const alternatives = findPath(env.open(), "alice", "bob",
            {Account("alice")["USD"]}, Account("bob")["USD"](5));
        Json::Value jv;
        Json::Reader().parse(R"([{
                "paths_canonical" : [],
                "paths_computed" : [],
                "source_amount" :
                {
                    "currency" : "USD",
                    "issuer" : "rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn",
                    "value" : "5"
                }
            }])", jv);
        expect(jv == alternatives);
    }

    void
    test_payment_auto_path_find()
    {
        testcase("payment auto path find");
        Env env(*this);
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];
        env.fund(XRP(10000), "alice", "bob", gw);
        env.trust(USD(600), "alice");
        env.trust(USD(700), "bob");
        env(pay(gw, "alice", USD(70)));
        env(pay("alice", "bob", USD(24)));
        env.require(balance("alice", USD(46)));
        env.require(balance(gw, Account("alice")["USD"](-46)));
        env.require(balance("bob", USD(24)));
        env.require(balance(gw, Account("bob")["USD"](-24)));
    }

    void
    test_path_find()
    {
        testcase("path find");
        Env env(*this);
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];
        env.fund(XRP(10000), "alice", "bob", gw);
        env.trust(USD(600), "alice");
        env.trust(USD(700), "bob");
        env(pay(gw, "alice", USD(70)));
        env(pay(gw, "bob", USD(50)));

        auto const alternatives = findPath(env.open(), "alice", "bob",
            {USD}, Account("bob")["USD"](5));
        Json::Value jv;
        Json::Reader().parse(R"([{
                "paths_canonical" : [],
                "paths_computed" : [],
                "source_amount" :
                {
                    "currency" : "USD",
                    "issuer" : "r9QxhA9RghPZBbUchA9HkrmLKaWvkLXU29",
                    "value" : "5"
                }
            }])", jv);
        expect(jv == alternatives);
    }

    void
    test_path_find_consume_all()
    {
        testcase("path find consume all");
        Env env(*this);
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];
        env.fund(XRP(10000), "alice", "bob", gw);
        env.trust(Account("alice")["USD"](600), gw);
        env.trust(USD(700), "bob");

        auto const alternatives = findPath(env.open(), "alice", "bob",
            {USD}, Account("bob")["USD"](1));
        Json::Value jv;
        Json::Reader().parse(R"([{
                "paths_canonical" : [],
                "paths_computed" : [],
                "source_amount" :
                {
                    "currency" : "USD",
                    "issuer" : "r9QxhA9RghPZBbUchA9HkrmLKaWvkLXU29",
                    "value" : "1"
                }
            }])", jv);
        expect(jv == alternatives);
    }

    void
    test_alternative_path_consume_both()
    {
        testcase("alternative path consume both");
        Env env(*this);
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];
        auto const gw2 = Account("gateway2");
        auto const gw2_USD = gw2["USD"];
        env.fund(XRP(10000), "alice", "bob", gw, gw2);
        env.trust(USD(600), "alice");
        env.trust(gw2_USD(800), "alice");
        env.trust(USD(700), "bob");
        env.trust(gw2_USD(900), "bob");
        env(pay(gw, "alice", USD(70)));
        env(pay(gw2, "alice", gw2_USD(70)));
        env(pay("alice", "bob", Account("bob")["USD"](140)),
            paths(Account("alice")["USD"]));
        env.require(balance("alice", USD(0)));
        env.require(balance("alice", gw2_USD(0)));
        env.require(balance("bob", USD(70)));
        env.require(balance("bob", gw2_USD(70)));
        env.require(balance(gw, Account("alice")["USD"](0)));
        env.require(balance(gw, Account("bob")["USD"](-70)));
        env.require(balance(gw2, Account("alice")["USD"](0)));
        env.require(balance(gw2, Account("bob")["USD"](-70)));
    }

    void
    test_alternative_paths_consume_best_transfer()
    {
        testcase("alternative paths consume best transfer");
        Env env(*this);
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];
        auto const gw2 = Account("gateway2");
        auto const gw2_USD = gw2["USD"];
        env.fund(XRP(10000), "alice", "bob", gw, gw2);
        env(rate(gw2, 1.1));
        env.trust(USD(600), "alice");
        env.trust(gw2_USD(800), "alice");
        env.trust(USD(700), "bob");
        env.trust(gw2_USD(900), "bob");
        env(pay(gw, "alice", USD(70)));
        env(pay(gw2, "alice", gw2_USD(70)));
        env(pay("alice", "bob", USD(70)));
        env.require(balance("alice", USD(0)));
        env.require(balance("alice", gw2_USD(70)));
        env.require(balance("bob", USD(70)));
        env.require(balance("bob", gw2_USD(0)));
        env.require(balance(gw, Account("alice")["USD"](0)));
        env.require(balance(gw, Account("bob")["USD"](-70)));
        env.require(balance(gw2, Account("alice")["USD"](-70)));
        env.require(balance(gw2, Account("bob")["USD"](0)));
    }

    void
    test_alternative_paths_consume_best_transfer_first()
    {
        testcase("alternative paths - consume best transfer first");
        Env env(*this);
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];
        auto const gw2 = Account("gateway2");
        auto const gw2_USD = gw2["USD"];
        env.fund(XRP(10000), "alice", "bob", gw, gw2);
        env(rate(gw2, 1.1));
        env.trust(USD(600), "alice");
        env.trust(gw2_USD(800), "alice");
        env.trust(USD(700), "bob");
        env.trust(gw2_USD(900), "bob");
        env(pay(gw, "alice", USD(70)));
        env(pay(gw2, "alice", gw2_USD(70)));
        env(pay("alice", "bob", Account("bob")["USD"](77)),
            sendmax(Account("alice")["USD"](100)),
                paths(Account("alice")["USD"]));
        env.require(balance("alice", USD(0)));
        env.require(balance("alice", gw2_USD(62.3)));
        env.require(balance("bob", USD(70)));
        env.require(balance("bob", gw2_USD(7)));
        env.require(balance(gw, Account("alice")["USD"](0)));
        env.require(balance(gw, Account("bob")["USD"](-70)));
        env.require(balance(gw2, Account("alice")["USD"](-62.3)));
        env.require(balance(gw2, Account("bob")["USD"](-7)));
    }

    void
    test_alternative_paths_limit_returned_paths_to_best_quality()
    {
        testcase("alternative paths - limit returned paths to best quality");
        Env env(*this);
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];
        auto const gw2 = Account("gateway2");
        auto const gw2_USD = gw2["USD"];
        env.fund(XRP(10000), "alice", "bob", "carol", "dan", gw, gw2);
        env(rate("carol", 1.1));
        env.trust(Account("carol")["USD"](800), "alice", "bob");
        env.trust(Account("dan")["USD"](800), "alice", "bob");
        env.trust(USD(800), "alice", "bob");
        env.trust(gw2_USD(800), "alice", "bob");
        env.trust(Account("alice")["USD"](800), "dan");
        env.trust(Account("bob")["USD"](800), "dan");
        env(pay(gw2, "alice", gw2_USD(100)));
        env(pay("carol", "alice", Account("carol")["USD"](100)));
        env(pay(gw, "alice", USD(100)));

        auto const alternatives = findPath(env.open(), "alice", "bob",
            {USD}, Account("bob")["USD"](5));
        Json::Value jv;
        Json::Reader().parse(R"([{
                "paths_canonical" : [],
                "paths_computed" : [],
                "source_amount" :
                {
                    "currency" : "USD",
                    "issuer" : "r9QxhA9RghPZBbUchA9HkrmLKaWvkLXU29",
                    "value" : "5"
                }
            }])", jv);
        expect(jv == alternatives);
    }

    void
    test_issues_path_negative_issue()
    {
        testcase("path negative: Issue #5");
        Env env(*this);
        env.fund(XRP(10000), "alice", "bob", "carol", "dan");
        env.trust(Account("bob")["USD"](100), "alice", "carol", "dan");
        env.trust(Account("alice")["USD"](100), "dan");
        env.trust(Account("carol")["USD"](100), "dan");
        env(pay("bob", "carol", Account("bob")["USD"](75)));
        env.require(balance("bob", Account("carol")["USD"](-75)));
        env.require(balance("carol", Account("bob")["USD"](75)));

        auto alternatives = findPath(env.open(), "alice", "bob",
            {Account("alice")["USD"]}, Account("bob")["USD"](25));
        expect(alternatives.size() == 0);

        env(pay("alice", "bob", Account("alice")["USD"](25)),
            ter(tecPATH_DRY));

        alternatives = findPath(env.open(), "alice", "bob",
            {Account("alice")["USD"]}, Account("alice")["USD"](25));
        expect(alternatives.size() == 0);

        env.require(balance("alice", Account("bob")["USD"](0)));
        env.require(balance("alice", Account("dan")["USD"](0)));
        env.require(balance("bob", Account("alice")["USD"](0)));
        env.require(balance("bob", Account("carol")["USD"](-75)));
        env.require(balance("bob", Account("dan")["USD"](0)));
        env.require(balance("carol", Account("bob")["USD"](75)));
        env.require(balance("carol", Account("dan")["USD"](0)));
        env.require(balance("dan", Account("alice")["USD"](0)));
        env.require(balance("dan", Account("bob")["USD"](0)));
        env.require(balance("dan", Account("carol")["USD"](0)));
    }

    // alice -- limit 40 --> bob
    // alice --> carol --> dan --> bob
    // Balance of 100 USD Bob - Balance of 37 USD -> Rod
    void
    test_issues_path_negative_ripple_client_issue_23_smaller()
    {
        testcase("path negative: ripple-client issue #23: smaller");
        Env env(*this);
        env.fund(XRP(10000), "alice", "bob", "carol", "dan");
        env.trust(Account("alice")["USD"](40), "bob");
        env.trust(Account("dan")["USD"](20), "bob");
        env.trust(Account("alice")["USD"](20), "carol");
        env.trust(Account("carol")["USD"](20), "dan");
        env(pay("alice", "bob", Account("bob")["USD"](55)),
            paths(Account("alice")["USD"]));
        env.require(balance("bob", Account("alice")["USD"](40)));
        env.require(balance("bob", Account("dan")["USD"](15)));
    }

    // alice -120 USD-> edward -25 USD-> bob
    // alice -25 USD-> carol -75 USD -> dan -100 USD-> bob
    void
    test_issues_path_negative_ripple_client_issue_23_larger()
    {
        testcase("path negative: ripple-client issue #23: larger");
        Env env(*this);
        env.fund(XRP(10000), "alice", "bob", "carol", "dan", "edward");
        env.trust(Account("alice")["USD"](120), "edward");
        env.trust(Account("edward")["USD"](25), "bob");
        env.trust(Account("dan")["USD"](100), "bob");
        env.trust(Account("alice")["USD"](25), "carol");
        env.trust(Account("carol")["USD"](75), "dan");
        env(pay("alice", "bob", Account("bob")["USD"](50)),
            paths(Account("alice")["USD"]));
        env.require(balance("alice", Account("edward")["USD"](-25)));
        env.require(balance("alice", Account("carol")["USD"](-25)));
        env.require(balance("bob", Account("edward")["USD"](25)));
        env.require(balance("bob", Account("dan")["USD"](25)));
        env.require(balance("carol", Account("alice")["USD"](25)));
        env.require(balance("carol", Account("dan")["USD"](-25)));
        env.require(balance("dan", Account("carol")["USD"](25)));
        env.require(balance("dan", Account("bob")["USD"](-25)));
    }

    // carol holds gateway AUD, sells gateway AUD for XRP
    // bob will hold gateway AUD
    // alice pays bob gateway AUD using XRP
    void
    test_via_offers_via_gateway()
    {
        testcase("via gateway");
        Env env(*this);
        auto const gw = Account("gateway");
        auto const AUD = gw["AUD"];
        env.fund(XRP(10000), "alice", "bob", "carol", gw);
        env(rate(gw, 1.1));
        env.trust(AUD(100), "bob");
        env.trust(AUD(100), "carol");
        env(pay(gw, "carol", AUD(50)));
        env(offer("carol", XRP(50), AUD(50)));
        env(pay("alice", "bob", AUD(10)), sendmax(XRP(100)), paths(XRP));
        env.require(balance("bob", AUD(10)));
        env.require(balance("carol", AUD(39)));

        auto const alternatives = findPath(env.open(), "alice", "bob",
            {Account("alice")["USD"]}, Account("bob")["USD"](25));
        expect(alternatives.size() == 0);
    }

    void
    test_indirect_paths_path_find()
    {
        testcase("path find");
        Env env(*this);
        env.fund(XRP(10000), "alice", "bob", "carol");
        env.trust(Account("alice")["USD"](1000), "bob");
        env.trust(Account("bob")["USD"](1000), "carol");

        auto const alternatives = findPath(env.open(), "alice", "carol",
            {Account("alice")["USD"]}, Account("carol")["USD"](5));
        Json::Value jv;
        Json::Reader().parse(R"([{
                "paths_canonical" : [],
                "paths_computed" :
                [
                    [
                        {
                            "account" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
                            "type" : 1,
                            "type_hex" : "0000000000000001"
                        }
                    ]
                ],
                "source_amount" :
                {
                    "currency" : "USD",
                    "issuer" : "rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn",
                    "value" : "5"
                }
            }])", jv);
        expect(jv == alternatives);
    }

    void
    test_quality_paths_quality_set_and_test()
    {
        testcase("quality set and test");
        Env env(*this);
        env.fund(XRP(10000), "alice", "bob");
        env(trust("bob", Account("alice")["USD"](1000)),
            json("{\"" + sfQualityIn.fieldName + "\": 2000}"),
                json("{\"" + sfQualityOut.fieldName + "\": 1400000000}"));

        Json::Value jv;
        Json::Reader().parse(R"({
                "Balance" : {
                    "currency" : "USD",
                    "issuer" : "rrrrrrrrrrrrrrrrrrrrBZbvji",
                    "value" : "0"
                },
                "Flags" : 131072,
                "HighLimit" : {
                    "currency" : "USD",
                    "issuer" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
                    "value" : "1000"
                },
                "HighNode" : "0000000000000000",
                "HighQualityIn" : 2000,
                "HighQualityOut" : 1400000000,
                "LedgerEntryType" : "RippleState",
                "LowLimit" : {
                    "currency" : "USD",
                    "issuer" : "rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn",
                    "value" : "0"
                },
                "LowNode" : "0000000000000000"
            })", jv);

        auto const jv_l = env.le(keylet::line(Account("bob").id(),
            Account("alice")["USD"].issue()))->getJson(0);
        for (auto it = jv.begin(); it != jv.end(); ++it)
            expect(*it == jv_l[it.memberName()]);
    }

    void
    test_trust_auto_clear_trust_normal_clear()
    {
        testcase("trust normal clear");
        Env env(*this);
        env.fund(XRP(10000), "alice", "bob");
        env.trust(Account("bob")["USD"](1000), "alice");
        env.trust(Account("alice")["USD"](1000), "bob");

        Json::Value jv;
        Json::Reader().parse(R"({
                "Balance" : {
                    "currency" : "USD",
                    "issuer" : "rrrrrrrrrrrrrrrrrrrrBZbvji",
                    "value" : "0"
                },
                "Flags" : 196608,
                "HighLimit" : {
                    "currency" : "USD",
                    "issuer" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
                    "value" : "1000"
                },
                "HighNode" : "0000000000000000",
                "LedgerEntryType" : "RippleState",
                "LowLimit" : {
                    "currency" : "USD",
                    "issuer" : "rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn",
                    "value" : "1000"
                },
                "LowNode" : "0000000000000000"
            })", jv);

        auto const jv_l = env.le(keylet::line(Account("bob").id(),
            Account("alice")["USD"].issue()))->getJson(0);
        for (auto it = jv.begin(); it != jv.end(); ++it)
            expect(*it == jv_l[it.memberName()]);

        env.trust(Account("bob")["USD"](0), "alice");
        env.trust(Account("alice")["USD"](0), "bob");
        expect(env.le(keylet::line(Account("bob").id(),
            Account("alice")["USD"].issue())) == nullptr);
    }

    void
    test_trust_auto_clear_trust_auto_clear()
    {
        testcase("trust auto clear");
        Env env(*this);
        env.fund(XRP(10000), "alice", "bob");
        env.trust(Account("bob")["USD"](1000), "alice");
        env(pay("bob", "alice", Account("bob")["USD"](50)));
        env.trust(Account("bob")["USD"](0), "alice");

        Json::Value jv;
        Json::Reader().parse(R"({
                "Balance" :
                {
                    "currency" : "USD",
                    "issuer" : "rrrrrrrrrrrrrrrrrrrrBZbvji",
                    "value" : "50"
                },
                "Flags" : 65536,
                "HighLimit" :
                {
                    "currency" : "USD",
                    "issuer" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
                    "value" : "0"
                },
                "HighNode" : "0000000000000000",
                "LedgerEntryType" : "RippleState",
                "LowLimit" :
                {
                    "currency" : "USD",
                    "issuer" : "rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn",
                    "value" : "0"
                },
                "LowNode" : "0000000000000000"
            })", jv);

        auto const jv_l = env.le(keylet::line(Account("alice").id(),
            Account("bob")["USD"].issue()))->getJson(0);
        for (auto it = jv.begin(); it != jv.end(); ++it)
            expect(*it == jv_l[it.memberName()]);

        env(pay("alice", "bob", Account("alice")["USD"](50)));
        expect(env.le(keylet::line(Account("alice").id(),
            Account("bob")["USD"].issue())) == nullptr);
    }

    void
    run()
    {
        test_no_direct_path_no_intermediary_no_alternatives();
        test_direct_path_no_intermediary();
        test_payment_auto_path_find();
        test_path_find();
        test_path_find_consume_all();
        test_alternative_path_consume_both();
        test_alternative_paths_consume_best_transfer();
        test_alternative_paths_consume_best_transfer_first();
        test_alternative_paths_limit_returned_paths_to_best_quality();
        test_issues_path_negative_issue();
        test_issues_path_negative_ripple_client_issue_23_smaller();
        test_issues_path_negative_ripple_client_issue_23_larger();
        test_via_offers_via_gateway();
        test_indirect_paths_path_find();
        test_quality_paths_quality_set_and_test();
        test_trust_auto_clear_trust_normal_clear();
        test_trust_auto_clear_trust_auto_clear();
    }
};

BEAST_DEFINE_TESTSUITE(Path,app,ripple)

} // test
} // ripple
