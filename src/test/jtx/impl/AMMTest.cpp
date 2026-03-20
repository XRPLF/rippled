#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/CaptureLogs.h>
#include <test/jtx/Env.h>
#include <test/jtx/pay.h>

#include <xrpld/rpc/RPCHandler.h>

#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/resource/Fees.h>

namespace xrpl {
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
    : gw_("gateway")
    , carol_("carol")
    , alice_("alice")
    , bob_("bob")
    , USD(gw_["USD"])
    , EUR(gw_["EUR"])
    , GBP(gw_["GBP"])
    , BTC(gw_["BTC"])
    , BAD(jtx::IOU(gw_, badCurrency()))
{
}

void
AMMTestBase::testAMM(
    std::function<void(jtx::AMM&, jtx::Env&)> const& cb,
    std::optional<std::pair<STAmount, STAmount>> const& pool,
    std::uint16_t tfee,
    std::optional<jtx::Ter> const& ter,
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
        NumberMantissaScaleGuard const sg{xrpl::MantissaRange::small};

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
            auto const defIOU = STAmount{a.issue(), 30000};
            if (a <= defIOU)
                return defIOU;
            return a + STAmount{a.issue(), 1000};
        };
        auto const toFund1 = toFund(asset1);
        auto const toFund2 = toFund(asset2);
        BEAST_EXPECT(asset1 <= toFund1 && asset2 <= toFund2);

        if (!asset1.native() && !asset2.native())
        {
            fund(env, gw_, {alice_, carol_}, {toFund1, toFund2}, Fund::All);
        }
        else if (asset1.native())
        {
            fund(env, gw_, {alice_, carol_}, toFund1, {toFund2}, Fund::All);
        }
        else if (asset2.native())
        {
            fund(env, gw_, {alice_, carol_}, toFund2, {toFund1}, Fund::All);
        }

        AMM ammAlice(
            env, alice_, asset1, asset2, CreateArg{.log = false, .tfee = arg.tfee, .err = arg.ter});
        if (BEAST_EXPECT(ammAlice.expectBalances(asset1, asset2, ammAlice.tokens())))
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
AMMTest::findPathsRequest(
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
    params[jss::destination_amount] = saDstAmount.getJson(JsonOptions::kNONE);
    if (saSendMax)
        params[jss::send_max] = saSendMax->getJson(JsonOptions::kNONE);
    if (saSrcCurrency)
    {
        auto& sc = params[jss::source_currencies] = Json::arrayValue;
        Json::Value j = Json::objectValue;
        j[jss::currency] = to_string(saSrcCurrency.value());
        sc.append(j);
    }

    Json::Value result;
    Gate g;
    app.getJobQueue().postCoro(jtCLIENT, "RPC-Client", [&](auto const& coro) {
        context.params = std::move(params);
        context.coro = coro;
        RPC::doCommand(context, result);
        g.signal();
    });

    using namespace std::chrono_literals;
    BEAST_EXPECT(g.waitFor(5s));
    BEAST_EXPECT(!result.isMember(jss::error));
    return result;
}

std::tuple<STPathSet, STAmount, STAmount>
AMMTest::findPaths(
    jtx::Env& env,
    jtx::Account const& src,
    jtx::Account const& dst,
    STAmount const& saDstAmount,
    std::optional<STAmount> const& saSendMax,
    std::optional<Currency> const& saSrcCurrency)
{
    Json::Value result = findPathsRequest(env, src, dst, saDstAmount, saSendMax, saSrcCurrency);
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
                // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
                paths = po.object->getFieldPathSet(sfPaths);
            }
        }
    }

    return std::make_tuple(std::move(paths), std::move(sa), std::move(da));
}

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
