//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <test/jtx/AMMTest.h>

#include <ripple/protocol/STParsedJSON.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <test/jtx/AMM.h>
#include <test/jtx/Env.h>
#include <test/jtx/pay.h>

namespace ripple {
namespace test {
namespace jtx {

void
fund(
    jtx::Env& env,
    jtx::Account const& gw,
    std::vector<jtx::Account> const& accounts,
    std::vector<STAmount> const& amts,
    Fund how)
{
    fund(env, gw, accounts, XRP(30000), amts, how);
}

void
fund(
    jtx::Env& env,
    std::vector<jtx::Account> const& accounts,
    STAmount const& xrp,
    std::vector<STAmount> const& amts,
    Fund how)
{
    for (auto const& account : accounts)
    {
        if (how == Fund::All || how == Fund::Acct)
        {
            env.fund(xrp, account);
        }
    }
    env.close();
    for (auto const& account : accounts)
    {
        for (auto const& amt : amts)
        {
            env.trust(amt + amt, account);
            env(pay(amt.issue().account, account, amt));
        }
    }
    env.close();
}

void
fund(
    jtx::Env& env,
    jtx::Account const& gw,
    std::vector<jtx::Account> const& accounts,
    STAmount const& xrp,
    std::vector<STAmount> const& amts,
    Fund how)
{
    if (how == Fund::All || how == Fund::Gw)
        env.fund(xrp, gw);
    env.close();
    fund(env, accounts, xrp, amts, how);
}

AMMTestBase::AMMTestBase()
    : gw("gateway")
    , carol("carol")
    , alice("alice")
    , bob("bob")
    , USD(gw["USD"])
    , EUR(gw["EUR"])
    , GBP(gw["GBP"])
    , BTC(gw["BTC"])
    , BAD(jtx::IOU(gw, badCurrency()))
{
}

void
AMMTestBase::testAMM(
    std::function<void(jtx::AMM&, jtx::Env&)>&& cb,
    std::optional<std::pair<STAmount, STAmount>> const& pool,
    std::uint16_t tfee,
    std::optional<jtx::ter> const& ter,
    std::vector<FeatureBitset> const& vfeatures)
{
    using namespace jtx;

    for (auto const& features : vfeatures)
    {
        Env env{*this, features};

        auto const [asset1, asset2] =
            pool ? *pool : std::make_pair(XRP(10000), USD(10000));
        auto tofund = [&](STAmount const& a) -> STAmount {
            if (a.native())
            {
                auto const defXRP = XRP(30000);
                if (a <= defXRP)
                    return defXRP;
                return a + XRP(1000);
            }
            auto const defIOU = STAmount{a.issue(), 30000};
            if (a <= defIOU)
                return defIOU;
            return a + STAmount{a.issue(), 1000};
        };
        auto const toFund1 = tofund(asset1);
        auto const toFund2 = tofund(asset2);
        BEAST_EXPECT(asset1 <= toFund1 && asset2 <= toFund2);

        if (!asset1.native() && !asset2.native())
            fund(env, gw, {alice, carol}, {toFund1, toFund2}, Fund::All);
        else if (asset1.native())
            fund(env, gw, {alice, carol}, toFund1, {toFund2}, Fund::All);
        else if (asset2.native())
            fund(env, gw, {alice, carol}, toFund2, {toFund1}, Fund::All);

        AMM ammAlice(
            env,
            alice,
            asset1,
            asset2,
            CreateArg{.log = false, .tfee = tfee, .err = ter});
        if (BEAST_EXPECT(
                ammAlice.expectBalances(asset1, asset2, ammAlice.tokens())))
            cb(ammAlice, env);
    }
}

XRPAmount
AMMTest::reserve(jtx::Env& env, std::uint32_t count) const
{
    return env.current()->fees().accountReserve(count);
}

XRPAmount
AMMTest::ammCrtFee(jtx::Env& env) const
{
    return env.current()->fees().increment;
}

jtx::Env
AMMTest::pathTestEnv()
{
    // These tests were originally written with search parameters that are
    // different from the current defaults. This function creates an env
    // with the search parameters that the tests were written for.
    return Env(*this, envconfig([](std::unique_ptr<Config> cfg) {
        cfg->PATH_SEARCH_OLD = 7;
        cfg->PATH_SEARCH = 7;
        cfg->PATH_SEARCH_MAX = 10;
        return cfg;
    }));
}

Json::Value
AMMTest::find_paths_request(
    jtx::Env& env,
    jtx::Account const& src,
    jtx::Account const& dst,
    STAmount const& saDstAmount,
    std::optional<STAmount> const& saSendMax,
    std::optional<Currency> const& saSrcCurrency)
{
    using namespace jtx;

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

    Json::Value params = Json::objectValue;
    params[jss::command] = "ripple_path_find";
    params[jss::source_account] = toBase58(src);
    params[jss::destination_account] = toBase58(dst);
    params[jss::destination_amount] = saDstAmount.getJson(JsonOptions::none);
    if (saSendMax)
        params[jss::send_max] = saSendMax->getJson(JsonOptions::none);
    if (saSrcCurrency)
    {
        auto& sc = params[jss::source_currencies] = Json::arrayValue;
        Json::Value j = Json::objectValue;
        j[jss::currency] = to_string(saSrcCurrency.value());
        sc.append(j);
    }

    Json::Value result;
    gate g;
    app.getJobQueue().postCoro(jtCLIENT, "RPC-Client", [&](auto const& coro) {
        context.params = std::move(params);
        context.coro = coro;
        RPC::doCommand(context, result);
        g.signal();
    });

    using namespace std::chrono_literals;
    BEAST_EXPECT(g.wait_for(5s));
    BEAST_EXPECT(!result.isMember(jss::error));
    return result;
}

std::tuple<STPathSet, STAmount, STAmount>
AMMTest::find_paths(
    jtx::Env& env,
    jtx::Account const& src,
    jtx::Account const& dst,
    STAmount const& saDstAmount,
    std::optional<STAmount> const& saSendMax,
    std::optional<Currency> const& saSrcCurrency)
{
    Json::Value result = find_paths_request(
        env, src, dst, saDstAmount, saSendMax, saSrcCurrency);
    BEAST_EXPECT(!result.isMember(jss::error));

    STAmount da;
    if (result.isMember(jss::destination_amount))
        da = amountFromJson(sfGeneric, result[jss::destination_amount]);

    STAmount sa;
    STPathSet paths;
    if (result.isMember(jss::alternatives))
    {
        auto const& alts = result[jss::alternatives];
        if (alts.size() > 0)
        {
            auto const& path = alts[0u];

            if (path.isMember(jss::source_amount))
                sa = amountFromJson(sfGeneric, path[jss::source_amount]);

            if (path.isMember(jss::destination_amount))
                da = amountFromJson(sfGeneric, path[jss::destination_amount]);

            if (path.isMember(jss::paths_computed))
            {
                Json::Value p;
                p["Paths"] = path[jss::paths_computed];
                STParsedJSONObject po("generic", p);
                paths = po.object->getFieldPathSet(sfPaths);
            }
        }
    }

    return std::make_tuple(std::move(paths), std::move(sa), std::move(da));
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
