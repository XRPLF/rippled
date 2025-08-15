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

#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/CaptureLogs.h>
#include <test/jtx/Env.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>

#include <xrpld/rpc/RPCHandler.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/resource/Fees.h>

namespace ripple {
namespace test {
namespace jtx {

[[maybe_unused]] std::vector<STAmount>
fund(
    jtx::Env& env,
    jtx::Account const& gw,
    std::vector<jtx::Account> const& accounts,
    std::vector<STAmount> const& amts,
    Fund how)
{
    return fund(env, gw, accounts, XRP(30000), amts, how);
}

[[maybe_unused]] std::vector<STAmount>
fund(
    jtx::Env& env,
    std::vector<jtx::Account> const& accounts,
    STAmount const& xrp,
    std::vector<STAmount> const& amts,
    Fund how,
    std::optional<Account> const& mptIssuer)
{
    for (auto const& account : accounts)
    {
        if (how == Fund::All || how == Fund::Acct)
        {
            env.fund(xrp, account);
        }
    }
    env.close();

    std::vector<STAmount> amtsOut;
    for (auto const& account : accounts)
    {
        int i = 0;
        for (auto const& amt : amts)
        {
            auto amt_ = [&]() {
                if (amtsOut.size() == amts.size())
                    return amtsOut[i++];
                else if (amt.holds<MPTIssue>() && mptIssuer)
                {
                    MPTTester mpt(
                        {.env = env,
                         .issuer = *mptIssuer,
                         .holders = accounts});
                    return STAmount{mpt.issuanceID(), amt.mpt().value()};
                }
                return amt;
            }();
            if (amt.holds<Issue>())
                env.trust(amt_ + amt_, account);
            if (amtsOut.size() != amts.size())
                amtsOut.push_back(amt_);
            env(pay(amt_.getIssuer(), account, amt_));
        }
    }
    env.close();
    return amtsOut;
}

[[maybe_unused]] std::vector<STAmount>
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
    return fund(env, accounts, xrp, amts, how, gw);
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
    testAMM(
        std::move(cb),
        TestAMMArg{
            .pool = pool, .tfee = tfee, .ter = ter, .features = vfeatures});
}

void
AMMTestBase::testAMM(
    std::function<void(jtx::AMM&, jtx::Env&)>&& cb,
    TestAMMArg const& arg)
{
    using namespace jtx;

    std::string logs;

    for (auto const& features : arg.features)
    {
        Env env{
            *this,
            features,
            arg.noLog ? std::make_unique<CaptureLogs>(&logs) : nullptr};

        auto const [asset1, asset2] =
            arg.pool ? *arg.pool : std::make_pair(XRP(10000), USD(10000));
        auto tofund = [&](STAmount const& a) -> STAmount {
            if (a.native())
            {
                auto const defXRP = XRP(30000);
                if (a <= defXRP)
                    return defXRP;
                return a + XRP(1000);
            }
            auto const defAmt = STAmount{a.asset(), 30000};
            if (a <= defAmt)
                return defAmt;
            return a + STAmount{a.asset(), 1000};
        };
        auto const toFund1 = tofund(asset1);
        auto const toFund2 = tofund(asset2);
        BEAST_EXPECT(asset1 <= toFund1 && asset2 <= toFund2);

        // asset1/asset2 could be dummy MPT. In this case real MPT
        // is created by fund(), which returns the funded amounts.
        // The amounts then can be used to figure out the created
        // MPT if any.
        std::vector<STAmount> funded;
        if (!asset1.native() && !asset2.native())
        {
            funded =
                fund(env, gw, {alice, carol}, {toFund1, toFund2}, Fund::All);
        }
        else if (asset1.native())
        {
            funded =
                fund(env, gw, {alice, carol}, toFund1, {toFund2}, Fund::All);
            funded.insert(funded.begin(), toFund1);
        }
        else if (asset2.native())
        {
            funded =
                fund(env, gw, {alice, carol}, toFund2, {toFund1}, Fund::All);
            funded.push_back(toFund2);
        }

        auto const pool1 =
            STAmount{funded[0].asset(), static_cast<Number>(asset1)};
        auto const pool2 =
            STAmount{funded[1].asset(), static_cast<Number>(asset2)};

        AMM ammAlice(
            env,
            alice,
            pool1,
            pool2,
            CreateArg{.log = false, .tfee = arg.tfee, .err = arg.ter});
        if (BEAST_EXPECT(
                ammAlice.expectBalances(pool1, pool2, ammAlice.tokens())))
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
}  // namespace jtx
}  // namespace test
}  // namespace ripple
