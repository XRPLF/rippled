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
#include <ripple/basics/Log.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/rpc/RipplePathFind.h>
#include <ripple/test/jtx.h>
#include <beast/unit_test/suite.h>
#include <ripple/app/paths/AccountCurrencies.h>

namespace ripple {
namespace test {

//------------------------------------------------------------------------------

namespace detail {

void
stpath_append_one (STPath& st,
    jtx::Account const& account)
{
    st.push_back(STPathElement({
        account.id(), boost::none, boost::none }));
}

template <class T>
std::enable_if_t<
    std::is_constructible<jtx::Account, T>::value>
stpath_append_one (STPath& st,
    T const& t)
{
    stpath_append_one(st, jtx::Account{ t });
}

void
stpath_append_one (STPath& st,
    jtx::IOU const& iou)
{
    st.push_back(STPathElement({
        iou.account.id(), iou.currency, boost::none }));
}

void
stpath_append_one (STPath& st,
    jtx::BookSpec const& book)
{
    st.push_back(STPathElement({
        boost::none, book.currency, book.account }));
}

inline
void
stpath_append (STPath& st)
{
}

template <class T, class... Args>
void
stpath_append (STPath& st,
    T const& t, Args const&... args)
{
    stpath_append_one(st, t);
    stpath_append(st, args...);
}

inline
void
stpathset_append (STPathSet& st)
{
}

template <class... Args>
void
stpathset_append(STPathSet& st,
    STPath const& p, Args const&... args)
{
    st.push_back(p);
    stpathset_append(st, args...);
}

} // detail

template <class... Args>
STPath
stpath (Args const&... args)
{
    STPath st;
    detail::stpath_append(st, args...);
    return st;
}

template <class... Args>
bool
same (STPathSet const& st1, Args const&... args)
{
    STPathSet st2;
    detail::stpathset_append(st2, args...);
    if (st1.size() != st2.size())
        return false;

    for (auto const& p : st2)
    {
        if (std::find(st1.begin(), st1.end(), p) == st1.end())
            return false;
    }
    return true;
}

bool
equal(STAmount const& sa1, STAmount const& sa2)
{
    return sa1 == sa2 &&
        sa1.issue().account == sa2.issue().account;
}

std::tuple <STPathSet, STAmount, STAmount>
find_paths(jtx::Env& env,
    jtx::Account const& src, jtx::Account const& dst,
        STAmount const& saDstAmount,
            boost::optional<STAmount> const& saSendMax = boost::none)
{
    static int const level = 8;
    auto const& view = env.open ();
    auto cache = std::make_shared<RippleLineCache>(view);
    auto currencies = accountSourceCurrencies(src, cache, true);
    auto jvSrcCurrencies = Json::Value(Json::arrayValue);
    for (auto const& c : currencies)
    {
        Json::Value jvCurrency = Json::objectValue;
        jvCurrency[jss::currency] = to_string(c);
        jvSrcCurrencies.append(jvCurrency);
    }

    auto result = ripplePathFind(cache, src.id(), dst.id(),
        saDstAmount, jvSrcCurrencies, boost::none, level, saSendMax,
            saDstAmount == STAmount(saDstAmount.issue(), 1u, 0, true), env.app ());
    if (! result.first)
    {
        throw std::runtime_error(
            "Path_test::findPath: ripplePathFind find failed");
    }

    auto const& jv = result.second[0u];
    Json::Value paths;
    paths["Paths"] = jv["paths_computed"];
    STParsedJSONObject stp("generic", paths);

    STAmount sa;
    if (jv.isMember(jss::source_amount))
        sa = amountFromJson(sfGeneric, jv[jss::source_amount]);

    STAmount da;
    if (jv.isMember(jss::destination_amount))
        da = amountFromJson(sfGeneric, jv[jss::destination_amount]);

    return std::make_tuple(
        std::move(stp.object->getFieldPathSet(sfPaths)),
            std::move(sa), std::move(da));
}

//------------------------------------------------------------------------------

class Path_test : public beast::unit_test::suite
{
public:
    void
    no_direct_path_no_intermediary_no_alternatives()
    {
        using namespace jtx;
        testcase("no direct path no intermediary no alternatives");
        Env env(*this);
        env.fund(XRP(10000), "alice", "bob");

        auto const result = find_paths(env,
            "alice", "bob", Account("bob")["USD"](5));
        expect(std::get<0>(result).empty());
    }

    void
    direct_path_no_intermediary()
    {
        using namespace jtx;
        testcase("direct path no intermediary");
        Env env(*this);
        env.fund(XRP(10000), "alice", "bob");
        env.trust(Account("alice")["USD"](700), "bob");

        STPathSet st;
        STAmount sa;
        std::tie(st, sa, std::ignore) = find_paths(env,
            "alice", "bob", Account("bob")["USD"](5));
        expect(st.empty());
        expect(equal(sa, Account("alice")["USD"](5)));
    }

    void
    payment_auto_path_find()
    {
        using namespace jtx;
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
    path_find()
    {
        using namespace jtx;
        testcase("path find");
        Env env(*this);
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];
        env.fund(XRP(10000), "alice", "bob", gw);
        env.trust(USD(600), "alice");
        env.trust(USD(700), "bob");
        env(pay(gw, "alice", USD(70)));
        env(pay(gw, "bob", USD(50)));

        STPathSet st;
        STAmount sa;
        std::tie(st, sa, std::ignore) = find_paths(env,
            "alice", "bob", Account("bob")["USD"](5));
        expect(same(st, stpath("gateway")));
        expect(equal(sa, Account("alice")["USD"](5)));
    }

    void
    path_find_consume_all()
    {
        using namespace jtx;
        testcase("path find consume all");

        {
            Env env(*this);
            env.fund(XRP(10000), "alice", "bob", "carol",
                "dan", "edward");
            env.trust(Account("alice")["USD"](10), "bob");
            env.trust(Account("bob")["USD"](10), "carol");
            env.trust(Account("carol")["USD"](10), "edward");
            env.trust(Account("alice")["USD"](100), "dan");
            env.trust(Account("dan")["USD"](100), "edward");

            STPathSet st;
            STAmount sa;
            STAmount da;
            std::tie(st, sa, da) = find_paths(env,
                "alice", "edward", Account("edward")["USD"](-1));
            expect(same(st, stpath("dan"), stpath("bob", "carol")));
            expect(equal(sa, Account("alice")["USD"](110)));
            expect(equal(da, Account("edward")["USD"](110)));
        }

        {
            Env env(*this);
            auto const gw = Account("gateway");
            auto const USD = gw["USD"];
            env.fund(XRP(10000), "alice", "bob", "carol", gw);
            env.trust(USD(100), "bob", "carol");
            env(pay(gw, "carol", USD(100)));
            env(offer("carol", XRP(100), USD(100)));

            STPathSet st;
            STAmount sa;
            STAmount da;
            std::tie(st, sa, da) = find_paths(env,
                "alice", "bob", Account("bob")["AUD"](-1),
                    boost::optional<STAmount>(XRP(100000000)));
            expect(st.empty());
            std::tie(st, sa, da) = find_paths(env,
                "alice", "bob", Account("bob")["USD"](-1),
                    boost::optional<STAmount>(XRP(100000000)));
            expect(sa == XRP(100));
            expect(equal(da, Account("bob")["USD"](100)));
        }
    }

    void
    alternative_path_consume_both()
    {
        using namespace jtx;
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
    alternative_paths_consume_best_transfer()
    {
        using namespace jtx;
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
    alternative_paths_consume_best_transfer_first()
    {
        using namespace jtx;
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
    alternative_paths_limit_returned_paths_to_best_quality()
    {
        using namespace jtx;
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

        STPathSet st;
        STAmount sa;
        std::tie(st, sa, std::ignore) = find_paths(env,
            "alice", "bob", Account("bob")["USD"](5));
        expect(same(st, stpath("gateway"), stpath("gateway2"),
            stpath("dan"), stpath("carol")));
        expect(equal(sa, Account("alice")["USD"](5)));
    }

    void
    issues_path_negative_issue()
    {
        using namespace jtx;
        testcase("path negative: Issue #5");
        Env env(*this);
        env.fund(XRP(10000), "alice", "bob", "carol", "dan");
        env.trust(Account("bob")["USD"](100), "alice", "carol", "dan");
        env.trust(Account("alice")["USD"](100), "dan");
        env.trust(Account("carol")["USD"](100), "dan");
        env(pay("bob", "carol", Account("bob")["USD"](75)));
        env.require(balance("bob", Account("carol")["USD"](-75)));
        env.require(balance("carol", Account("bob")["USD"](75)));

        auto result = find_paths(env,
            "alice", "bob", Account("bob")["USD"](25));
        expect(std::get<0>(result).empty());

        env(pay("alice", "bob", Account("alice")["USD"](25)),
            ter(tecPATH_DRY));

        result = find_paths(env,
            "alice", "bob", Account("alice")["USD"](25));
        expect(std::get<0>(result).empty());

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
    issues_path_negative_ripple_client_issue_23_smaller()
    {
        using namespace jtx;
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
    issues_path_negative_ripple_client_issue_23_larger()
    {
        using namespace jtx;
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
    via_offers_via_gateway()
    {
        using namespace jtx;
        testcase("via gateway");
        Env env(*this);
        auto const gw = Account("gateway");
        auto const AUD = gw["AUD"];
        env.fund(XRP(10000), "alice", "bob", "carol", gw);
        env(rate(gw, 1.1));
        env.trust(AUD(100), "bob", "carol");
        env(pay(gw, "carol", AUD(50)));
        env(offer("carol", XRP(50), AUD(50)));
        env(pay("alice", "bob", AUD(10)), sendmax(XRP(100)), paths(XRP));
        env.require(balance("bob", AUD(10)));
        env.require(balance("carol", AUD(39)));

        auto const result = find_paths(env,
            "alice", "bob", Account("bob")["USD"](25));
        expect(std::get<0>(result).empty());
    }

    void
    indirect_paths_path_find()
    {
        using namespace jtx;
        testcase("path find");
        Env env(*this);
        env.fund(XRP(10000), "alice", "bob", "carol");
        env.trust(Account("alice")["USD"](1000), "bob");
        env.trust(Account("bob")["USD"](1000), "carol");

        STPathSet st;
        STAmount sa;
        std::tie(st, sa, std::ignore) = find_paths(env,
            "alice", "carol", Account("carol")["USD"](5));
        expect(same(st, stpath("bob")));
        expect(equal(sa, Account("alice")["USD"](5)));
    }

    void
    quality_paths_quality_set_and_test()
    {
        using namespace jtx;
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
    trust_auto_clear_trust_normal_clear()
    {
        using namespace jtx;
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
    trust_auto_clear_trust_auto_clear()
    {
        using namespace jtx;
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
        no_direct_path_no_intermediary_no_alternatives();
        direct_path_no_intermediary();
        payment_auto_path_find();
        path_find();
        path_find_consume_all();
        alternative_path_consume_both();
        alternative_paths_consume_best_transfer();
        alternative_paths_consume_best_transfer_first();
        alternative_paths_limit_returned_paths_to_best_quality();
        issues_path_negative_issue();
        issues_path_negative_ripple_client_issue_23_smaller();
        issues_path_negative_ripple_client_issue_23_larger();
        via_offers_via_gateway();
        indirect_paths_path_find();
        quality_paths_quality_set_and_test();
        trust_auto_clear_trust_normal_clear();
        trust_auto_clear_trust_auto_clear();
    }
};

BEAST_DEFINE_TESTSUITE(Path,app,ripple)

} // test
} // ripple
