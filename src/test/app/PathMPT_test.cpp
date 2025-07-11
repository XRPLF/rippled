//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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
#include <test/jtx/AMM.h>
#include <test/jtx/envconfig.h>

#include <xrpld/app/paths/AccountAssets.h>
#include <xrpld/core/JobQueue.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/RPCHandler.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/contract.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

namespace ripple {
namespace test {
namespace detail {

static Json::Value
rpf(jtx::Account const& src,
    jtx::Account const& dst,
    ripple::test::jtx::MPT const& USD,
    std::vector<MPTID> const& num_src)
{
    Json::Value jv = Json::objectValue;
    jv[jss::command] = "ripple_path_find";
    jv[jss::source_account] = toBase58(src);

    if (!num_src.empty())
    {
        auto& sc = (jv[jss::source_currencies] = Json::arrayValue);
        Json::Value j = Json::objectValue;
        for (auto const& id : num_src)
        {
            j[jss::mpt_issuance_id] = to_string(id);
            sc.append(j);
        }
    }

    auto const d = toBase58(dst);
    jv[jss::destination_account] = d;

    Json::Value& j = (jv[jss::destination_amount] = Json::objectValue);
    j[jss::mpt_issuance_id] = to_string(USD.mpt());
    j[jss::value] = "1";

    return jv;
}

}  // namespace detail

//------------------------------------------------------------------------------

class PathMPT_test : public beast::unit_test::suite
{
    jtx::Env
    pathTestEnv()
    {
        // These tests were originally written with search parameters that are
        // different from the current defaults. This function creates an env
        // with the search parameters that the tests were written for.
        using namespace jtx;
        return Env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->PATH_SEARCH_OLD = 7;
            cfg->PATH_SEARCH = 7;
            cfg->PATH_SEARCH_MAX = 10;
            return cfg;
        }));
    }

public:
    void
    source_currencies_limit()
    {
        testcase("source currency limits");
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(10'000), "alice", "bob", gw);

        MPT const USD = MPTTester(
            {.env = env, .issuer = gw, .holders = {alice, bob}, .maxAmt = 100});

        auto& app = env.app();
        Resource::Charge loadType = Resource::feeReferenceRPC;
        Resource::Consumer c;

        RPC::JsonContext context{
            {env.journal,
             app,
             loadType,
             app.getOPs(),
             app.getLedgerMaster(),
             c,
             Role::USER,
             {},
             {},
             RPC::apiVersionIfUnspecified},
            {},
            {}};
        Json::Value result;
        gate g;
        // Test RPC::Tuning::max_src_cur source currencies.
        std::vector<MPTID> num_src;
        for (std::uint8_t i = 0; i < RPC::Tuning::max_src_cur; ++i)
            num_src.push_back(makeMptID(i, bob));
        app.getJobQueue().postCoro(
            jtCLIENT, "RPC-Client", [&](auto const& coro) {
                context.params =
                    ripple::test::detail::rpf(alice, bob, USD, num_src);
                context.coro = coro;
                RPC::doCommand(context, result);
                g.signal();
            });
        BEAST_EXPECT(g.wait_for(5s));
        BEAST_EXPECT(!result.isMember(jss::error));

        // Test more than RPC::Tuning::max_src_cur source currencies.
        num_src.push_back(makeMptID(RPC::Tuning::max_src_cur, bob));
        app.getJobQueue().postCoro(
            jtCLIENT, "RPC-Client", [&](auto const& coro) {
                context.params =
                    ripple::test::detail::rpf(alice, bob, USD, num_src);
                context.coro = coro;
                RPC::doCommand(context, result);
                g.signal();
            });
        BEAST_EXPECT(g.wait_for(5s));
        BEAST_EXPECT(result.isMember(jss::error));

        // Test RPC::Tuning::max_auto_src_cur source currencies.
        num_src.clear();
        for (auto i = 0; i < (RPC::Tuning::max_auto_src_cur - 1); ++i)
        {
            auto CURM =
                MPTTester({.env = env, .issuer = alice, .holders = {bob}});
            num_src.push_back(CURM.issuanceID());
        }
        app.getJobQueue().postCoro(
            jtCLIENT, "RPC-Client", [&](auto const& coro) {
                context.params = ripple::test::detail::rpf(alice, bob, USD, {});
                context.coro = coro;
                RPC::doCommand(context, result);
                g.signal();
            });
        BEAST_EXPECT(g.wait_for(5s));
        BEAST_EXPECT(!result.isMember(jss::error));

        // Test more than RPC::Tuning::max_auto_src_cur source currencies.
        auto CURM = MPTTester({.env = env, .issuer = alice, .holders = {bob}});
        app.getJobQueue().postCoro(
            jtCLIENT, "RPC-Client", [&](auto const& coro) {
                context.params = ripple::test::detail::rpf(alice, bob, USD, {});
                context.coro = coro;
                RPC::doCommand(context, result);
                g.signal();
            });
        BEAST_EXPECT(g.wait_for(5s));
        BEAST_EXPECT(result.isMember(jss::error));
    }

    void
    no_direct_path_no_intermediary_no_alternatives()
    {
        testcase("no direct path no intermediary no alternatives");
        using namespace jtx;

        Env env = pathTestEnv();

        env.fund(XRP(10'000), "alice", "bob");

        auto USDM = MPTTester({.env = env, .issuer = "bob"});

        auto const result = find_paths(env, "alice", "bob", USDM(5));
        BEAST_EXPECT(std::get<0>(result).empty());
    }

    void
    direct_path_no_intermediary()
    {
        testcase("direct path no intermediary");
        using namespace jtx;
        Env env = pathTestEnv();
        env.fund(XRP(10'000), "alice", "bob");

        MPT const USD =
            MPTTester({.env = env, .issuer = "alice", .holders = {"bob"}});

        STPathSet st;
        STAmount sa;
        std::tie(st, sa, std::ignore) = find_paths(env, "alice", "bob", USD(5));
        BEAST_EXPECT(st.empty());
        BEAST_EXPECT(equal(sa, USD(5)));
    }

    void
    payment_auto_path_find()
    {
        testcase("payment auto path find");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gateway");
        env.fund(XRP(10'000), "alice", "bob", gw);
        MPT const USD =
            MPTTester({.env = env, .issuer = gw, .holders = {"alice", "bob"}});
        env(pay(gw, "alice", USD(70)));
        env(pay("alice", "bob", USD(24)));
        env.require(balance("alice", USD(46)));
        env.require(balance("bob", USD(24)));
    }

    void
    path_find()
    {
        testcase("path find");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gateway");
        env.fund(XRP(10'000), "alice", "bob", gw);
        MPT const USD =
            MPTTester({.env = env, .issuer = gw, .holders = {"alice", "bob"}});
        env(pay(gw, "alice", USD(70)));
        env(pay(gw, "bob", USD(50)));

        STPathSet st;
        STAmount sa;
        STAmount da;
        std::tie(st, sa, da) = find_paths(env, "alice", "bob", USD(5));
        // Note, a direct IOU payment will have "gateway" as alternative path
        // since IOU supports rippling
        BEAST_EXPECT(st.empty());
        BEAST_EXPECT(equal(sa, USD(5)));
        BEAST_EXPECT(equal(da, USD(5)));
    }

    void
    path_find_consume_all()
    {
        testcase("path find consume all");
        using namespace jtx;

        {
            Env env = pathTestEnv();
            auto const gw = Account("gateway");
            env.fund(XRP(10'000), "alice", "bob", "carol", gw);
            MPT const USD = MPTTester(
                {.env = env, .issuer = gw, .holders = {"bob", "carol"}});
            MPT const AUD(makeMptID(0, gw));
            env(pay(gw, "carol", USD(100)));
            env(offer("carol", XRP(100), USD(100)));

            STPathSet st;
            STAmount sa;
            STAmount da;
            std::tie(st, sa, da) = find_paths(
                env,
                "alice",
                "bob",
                AUD(-1),
                std::optional<STAmount>(XRP(100'000'000)));
            BEAST_EXPECT(st.empty());
            std::tie(st, sa, da) = find_paths(
                env,
                "alice",
                "bob",
                USD(-1),
                std::optional<STAmount>(XRP(100'000'000)));
            if (BEAST_EXPECT(st.size() == 1 && st[0].size() == 1))
            {
                auto const& pathElem = st[0][0];
                BEAST_EXPECT(
                    pathElem.isOffer() && pathElem.getIssuerID() == gw.id() &&
                    pathElem.getMPTID() == USD.issuanceID);
            }
            BEAST_EXPECT(sa == XRP(100));
            BEAST_EXPECT(equal(da, USD(100)));
        }
    }

    void
    alternative_paths_consume_best_transfer()
    {
        testcase("alternative paths consume best transfer");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gateway");
        auto const gw2 = Account("gateway2");
        env.fund(XRP(10'000), "alice", "bob", gw, gw2);
        MPT const USD =
            MPTTester({.env = env, .issuer = gw, .holders = {"alice", "bob"}});
        MPT const gw2_USD = MPTTester(
            {.env = env,
             .issuer = gw2,
             .holders = {"alice", "bob"},
             .transferFee = 1'000});
        env(pay(gw, "alice", USD(70)));
        env(pay(gw2, "alice", gw2_USD(70)));
        env(pay("alice", "bob", USD(70)));
        env.require(balance("alice", USD(0)));
        env.require(balance("alice", gw2_USD(70)));
        env.require(balance("bob", USD(70)));
        env.require(balance("bob", gw2_USD(0)));
    }

    void
    receive_max()
    {
        testcase("Receive max");
        using namespace jtx;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const charlie = Account("charlie");
        auto const gw = Account("gw");
        {
            // XRP -> IOU receive max
            Env env = pathTestEnv();
            env.fund(XRP(10'000), alice, bob, charlie, gw);
            env.close();
            MPT const USD = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, charlie}});
            env(pay(gw, charlie, USD(10)));
            env.close();
            env(offer(charlie, XRP(10), USD(10)));
            env.close();
            auto [st, sa, da] =
                find_paths(env, alice, bob, USD(-1), XRP(100).value());
            BEAST_EXPECT(sa == XRP(10));
            BEAST_EXPECT(equal(da, USD(10)));
            if (BEAST_EXPECT(st.size() == 1 && st[0].size() == 1))
            {
                auto const& pathElem = st[0][0];
                BEAST_EXPECT(
                    pathElem.isOffer() && pathElem.getIssuerID() == gw.id() &&
                    pathElem.getMPTID() == USD.mpt());
            }
        }
        {
            // IOU -> XRP receive max
            Env env = pathTestEnv();
            env.fund(XRP(10'000), alice, bob, charlie, gw);
            env.close();
            MPT const USD = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, charlie}});
            env(pay(gw, alice, USD(10)));
            env.close();
            env(offer(charlie, USD(10), XRP(10)));
            env.close();
            auto [st, sa, da] =
                find_paths(env, alice, bob, drops(-1), USD(100).value());
            BEAST_EXPECT(sa == USD(10));
            BEAST_EXPECT(equal(da, XRP(10)));
            if (BEAST_EXPECT(st.size() == 1 && st[0].size() == 1))
            {
                auto const& pathElem = st[0][0];
                BEAST_EXPECT(
                    pathElem.isOffer() &&
                    pathElem.getIssuerID() == xrpAccount() &&
                    pathElem.getCurrency() == xrpCurrency());
            }
        }
    }

    void
    run() override
    {
        source_currencies_limit();
        no_direct_path_no_intermediary_no_alternatives();
        direct_path_no_intermediary();
        payment_auto_path_find();
        path_find();
        path_find_consume_all();
        alternative_paths_consume_best_transfer();
        receive_max();
    }
};

BEAST_DEFINE_TESTSUITE(PathMPT, app, ripple);

}  // namespace test
}  // namespace ripple
