#include <test/jtx/AMMTest.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <string>
#include <vector>

namespace xrpl::test {

struct AMMTicks_test : public jtx::AMMTestBase
{
private:
    static void
    fundForAMMCreate(
        jtx::Env& env,
        jtx::Account const& gw,
        jtx::Account const& acct,
        jtx::IOU const& usd,
        jtx::IOU const& eur)
    {
        using namespace jtx;
        env.fund(XRP(100000), gw, acct);
        env.trust(usd(1000000), acct);
        env.trust(eur(1000000), acct);
        env(pay(gw, acct, usd(100000)));
        env(pay(gw, acct, eur(100000)));
        env.close();
    }

    static json::Value
    ammCreateJV(
        jtx::Env& env,
        jtx::Account const& acct,
        jtx::IOU const& asset1,
        jtx::IOU const& asset2,
        STAmount const& amt1,
        STAmount const& amt2,
        std::uint8_t curveType)
    {
        json::Value jv;
        jv[jss::Account] = acct.human();
        jv[jss::Amount] = amt1.getJson(JsonOptions::Values::None);
        jv[jss::Amount2] = amt2.getJson(JsonOptions::Values::None);
        jv[jss::TradingFee] = 0;
        jv[jss::TransactionType] = jss::AMMCreate;
        jv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        jv[sfCurveType.jsonName] = curveType;
        if (curveType == CtConcentratedLiquidity)
            jv[sfFeeTier.jsonName] = FtMedium;
        return jv;
    }

    // Insert a synthetic AMM_TICK SLE for `tickIndex` belonging to `ammID`.
    static void
    insertTick(
        jtx::Env& env,
        uint256 const& ammID,
        std::int32_t tickIndex,
        std::uint64_t liquidityNet,
        std::uint64_t liquidityGross)
    {
        env.app().getOpenLedger().modify(
            [&](OpenView& view, beast::Journal) -> bool {
                auto const k = keylet::ammTick(ammID, tickIndex);
                auto sle = std::make_shared<SLE>(k);
                (*sle)[sfAMMID] = ammID;
                sle->setFieldI32(sfTickIndex, tickIndex);
                sle->setFieldU64(sfLiquidityNet, liquidityNet);
                sle->setFieldU64(sfLiquidityGross, liquidityGross);
                sle->setFieldNumber(
                    sfFeeGrowthOutside0, STNumber{sfFeeGrowthOutside0, Number{0}});
                sle->setFieldNumber(
                    sfFeeGrowthOutside1, STNumber{sfFeeGrowthOutside1, Number{0}});
                view.rawInsert(sle);
                return true;
            });
    }

    static json::Value
    ticksRpcJv(
        jtx::IOU const& asset1,
        jtx::IOU const& asset2,
        std::uint8_t curveType,
        std::optional<std::int32_t> tickLower = std::nullopt,
        std::optional<std::int32_t> tickUpper = std::nullopt,
        std::optional<unsigned int> limit = std::nullopt,
        std::optional<std::string> marker = std::nullopt)
    {
        json::Value jv;
        jv[jss::asset] = STIssue(sfAsset, asset1.asset()).getJson(JsonOptions::Values::None);
        jv[jss::asset2] = STIssue(sfAsset, asset2.asset()).getJson(JsonOptions::Values::None);
        jv[jss::curve_type] = curveType;
        if (tickLower)
            jv[jss::tick_lower] = *tickLower;
        if (tickUpper)
            jv[jss::tick_upper] = *tickUpper;
        if (limit)
            jv[jss::limit] = *limit;
        if (marker)
            jv[jss::marker] = *marker;
        return jv;
    }

    void
    testEmpty(FeatureBitset features)
    {
        testcase("amm_ticks - empty CL pool");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const al("alice");
        Account const gw2("gateway");
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        fundForAMMCreate(env, gw2, al, usd, eur);

        auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000), CtConcentratedLiquidity);
        env(jv);
        env.close();

        auto const r = env.rpc(
            "json",
            "amm_ticks",
            to_string(ticksRpcJv(usd, eur, CtConcentratedLiquidity)))[jss::result];
        BEAST_EXPECT(!r.isMember(jss::error));
        BEAST_EXPECT(r[jss::ticks].isArray());
        BEAST_EXPECT(r[jss::ticks].size() == 0);
        BEAST_EXPECT(!r.isMember(jss::marker));
        BEAST_EXPECT(r.isMember(jss::amm_id));
        BEAST_EXPECT(r.isMember(jss::current_tick));
    }

    void
    testEnumerate(FeatureBitset features)
    {
        testcase("amm_ticks - enumerate initialized ticks");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const al("alice");
        Account const gw2("gateway");
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        fundForAMMCreate(env, gw2, al, usd, eur);

        auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000), CtConcentratedLiquidity);
        env(jv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtConcentratedLiquidity));
        BEAST_EXPECT(ammSle != nullptr);
        if (!ammSle)
            return;
        auto const ammID = ammSle->key();

        std::vector<std::int32_t> const ticks = {-600, -60, 0, 60, 600};
        for (auto const t : ticks)
            insertTick(env, ammID, t, 1000, 2000);

        auto const r = env.rpc(
            "json",
            "amm_ticks",
            to_string(ticksRpcJv(usd, eur, CtConcentratedLiquidity)))[jss::result];
        BEAST_EXPECT(!r.isMember(jss::error));
        BEAST_EXPECT(r[jss::ticks].size() == ticks.size());
        if (r[jss::ticks].size() == ticks.size())
        {
            for (unsigned int i = 0; i < ticks.size(); ++i)
            {
                BEAST_EXPECT(r[jss::ticks][i][jss::tick_index].asInt() == ticks[i]);
                BEAST_EXPECT(r[jss::ticks][i][jss::liquidity_net].asString() == "1000");
                BEAST_EXPECT(r[jss::ticks][i][jss::liquidity_gross].asString() == "2000");
                BEAST_EXPECT(r[jss::ticks][i].isMember(jss::fee_growth_outside_0));
                BEAST_EXPECT(r[jss::ticks][i].isMember(jss::fee_growth_outside_1));
                BEAST_EXPECT(r[jss::ticks][i].isMember(jss::index));
            }
        }
        BEAST_EXPECT(!r.isMember(jss::marker));
        BEAST_EXPECT(r[jss::amm_id].asString() == to_string(ammID));
    }

    void
    testRangeFilter(FeatureBitset features)
    {
        testcase("amm_ticks - tick_lower / tick_upper filter");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const al("alice");
        Account const gw2("gateway");
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        fundForAMMCreate(env, gw2, al, usd, eur);

        auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000), CtConcentratedLiquidity);
        env(jv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtConcentratedLiquidity));
        BEAST_EXPECT(ammSle != nullptr);
        if (!ammSle)
            return;
        auto const ammID = ammSle->key();

        for (auto t : {-600, -60, 0, 60, 600})
            insertTick(env, ammID, t, 1000, 2000);

        // Filter to [-100, 100] -> only -60, 0, 60.
        auto const r = env.rpc(
            "json",
            "amm_ticks",
            to_string(ticksRpcJv(usd, eur, CtConcentratedLiquidity, -100, 100)))[jss::result];
        BEAST_EXPECT(!r.isMember(jss::error));
        BEAST_EXPECT(r[jss::ticks].size() == 3);
        if (r[jss::ticks].size() == 3)
        {
            BEAST_EXPECT(r[jss::ticks][0u][jss::tick_index].asInt() == -60);
            BEAST_EXPECT(r[jss::ticks][1u][jss::tick_index].asInt() == 0);
            BEAST_EXPECT(r[jss::ticks][2u][jss::tick_index].asInt() == 60);
        }
    }

    void
    testPagination(FeatureBitset features)
    {
        testcase("amm_ticks - limit / marker pagination");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const al("alice");
        Account const gw2("gateway");
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        fundForAMMCreate(env, gw2, al, usd, eur);

        auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000), CtConcentratedLiquidity);
        env(jv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtConcentratedLiquidity));
        BEAST_EXPECT(ammSle != nullptr);
        if (!ammSle)
            return;
        auto const ammID = ammSle->key();

        std::vector<std::int32_t> const ticks = {-600, -60, 0, 60, 600};
        for (auto t : ticks)
            insertTick(env, ammID, t, 1, 1);

        // First page: limit 2.
        auto const page1 = env.rpc(
            "json",
            "amm_ticks",
            to_string(ticksRpcJv(
                usd, eur, CtConcentratedLiquidity, std::nullopt, std::nullopt, 2u)))[jss::result];
        BEAST_EXPECT(!page1.isMember(jss::error));
        BEAST_EXPECT(page1[jss::ticks].size() == 2);
        BEAST_EXPECT(page1.isMember(jss::marker));
        if (!page1.isMember(jss::marker))
            return;
        BEAST_EXPECT(page1[jss::ticks][0u][jss::tick_index].asInt() == -600);
        BEAST_EXPECT(page1[jss::ticks][1u][jss::tick_index].asInt() == -60);

        // Page 2 from marker.
        auto const page2 = env.rpc(
            "json",
            "amm_ticks",
            to_string(ticksRpcJv(
                usd,
                eur,
                CtConcentratedLiquidity,
                std::nullopt,
                std::nullopt,
                2u,
                page1[jss::marker].asString())))[jss::result];
        BEAST_EXPECT(!page2.isMember(jss::error));
        BEAST_EXPECT(page2[jss::ticks].size() == 2);
        BEAST_EXPECT(page2.isMember(jss::marker));
        if (!page2.isMember(jss::marker))
            return;
        BEAST_EXPECT(page2[jss::ticks][0u][jss::tick_index].asInt() == 0);
        BEAST_EXPECT(page2[jss::ticks][1u][jss::tick_index].asInt() == 60);

        // Page 3 from marker -> final tick, no marker.
        auto const page3 = env.rpc(
            "json",
            "amm_ticks",
            to_string(ticksRpcJv(
                usd,
                eur,
                CtConcentratedLiquidity,
                std::nullopt,
                std::nullopt,
                2u,
                page2[jss::marker].asString())))[jss::result];
        BEAST_EXPECT(!page3.isMember(jss::error));
        BEAST_EXPECT(page3[jss::ticks].size() == 1);
        BEAST_EXPECT(page3[jss::ticks][0u][jss::tick_index].asInt() == 600);
        BEAST_EXPECT(!page3.isMember(jss::marker));
    }

    void
    testNonCLPoolRejected(FeatureBitset features)
    {
        testcase("amm_ticks - non-CL pool rejected");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const al("alice");
        Account const gw2("gateway");
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        fundForAMMCreate(env, gw2, al, usd, eur);

        auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000), CtConstantProduct);
        env(jv);
        env.close();

        // curve_type 0 -> should error.
        auto const r = env.rpc(
            "json",
            "amm_ticks",
            to_string(ticksRpcJv(usd, eur, CtConstantProduct)))[jss::result];
        BEAST_EXPECT(r.isMember(jss::error));
    }

public:
    void
    run() override
    {
        auto const features = testableAmendments();
        testEmpty(features);
        testEnumerate(features);
        testRangeFilter(features);
        testPagination(features);
        testNonCLPoolRejected(features);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(AMMTicks, app, xrpl, 1);

}  // namespace xrpl::test
