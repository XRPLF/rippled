#include <test/jtx/AMMTest.h>

#include <test/jtx/AMM.h>
#include <test/jtx/Account.h>
#include <test/jtx/CaptureLogs.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>

#include <xrpld/core/Config.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace xrpl::test::jtx {

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
                {
                    return amtsOut[i++];
                }
                if (amt.holds<MPTIssue>() && mptIssuer)
                {
                    MPTTester const mpt({.env = env, .issuer = *mptIssuer, .holders = accounts});
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
    std::function<void(jtx::AMM&, jtx::Env&)> const& cb,
    std::optional<std::pair<STAmount, STAmount>> const& pool,
    std::uint16_t tfee,
    std::optional<jtx::ter> const& ter,
    std::vector<FeatureBitset> const& vfeatures)
{
    testAMM(cb, TestAMMArg{.pool = pool, .tfee = tfee, .ter = ter, .features = vfeatures});
}

void
AMMTestBase::testAMM(std::function<void(jtx::AMM&, jtx::Env&)> const& cb, TestAMMArg const& arg)
{
    using namespace jtx;

    std::string logs;

    for (auto const& features : arg.features)
    {
        // Use small Number mantissas for the life of this test.
        NumberMantissaScaleGuard const sg{xrpl::MantissaRange::mantissa_scale::small};

        // For now, just disable SAV entirely, which locks in the small Number
        // mantissas
        Env env{
            *this,
            features - featureSingleAssetVault - featureLendingProtocol,
            arg.noLog ? std::make_unique<CaptureLogs>(&logs) : nullptr};

        auto const [asset1, asset2] = arg.pool ? *arg.pool : std::make_pair(XRP(10000), USD(10000));
        auto toFund = [&](STAmount const& a) -> STAmount {
            if (a.native())
            {
                auto const defXRP = XRP(30000);
                if (a <= defXRP)
                    return defXRP;
                return a + XRP(1000);
            }
            auto defAmt = STAmount{a.asset(), 30000};
            if (a <= defAmt)
                return defAmt;
            return a + STAmount{a.asset(), 1000};
        };
        auto const toFund1 = toFund(asset1);
        auto const toFund2 = toFund(asset2);
        BEAST_EXPECT(asset1 <= toFund1 && asset2 <= toFund2);

        // asset1/asset2 could be dummy MPT. In this case real MPT
        // is created by fund(), which returns the funded amounts.
        // The amounts then can be used to figure out the created
        // MPT if any.
        std::vector<STAmount> funded;
        if (!asset1.native() && !asset2.native())
        {
            funded = fund(env, gw, {alice, carol}, {toFund1, toFund2}, Fund::All);
        }
        else if (asset1.native())
        {
            funded = fund(env, gw, {alice, carol}, toFund1, {toFund2}, Fund::All);
            funded.insert(funded.begin(), toFund1);
        }
        else if (asset2.native())
        {
            funded = fund(env, gw, {alice, carol}, toFund2, {toFund1}, Fund::All);
            funded.push_back(toFund2);
        }

        auto const pool1 = STAmount{funded[0].asset(), static_cast<Number>(asset1)};
        auto const pool2 = STAmount{funded[1].asset(), static_cast<Number>(asset2)};

        AMM ammAlice(
            env, alice, pool1, pool2, CreateArg{.log = false, .tfee = arg.tfee, .err = arg.ter});
        if (BEAST_EXPECT(ammAlice.expectBalances(pool1, pool2, ammAlice.tokens())))
            cb(ammAlice, env);
    }
}

XRPAmount
AMMTest::reserve(jtx::Env& env, std::uint32_t count)
{
    return env.current()->fees().accountReserve(count);
}

XRPAmount
AMMTest::ammCrtFee(jtx::Env& env)
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
}  // namespace xrpl::test::jtx
