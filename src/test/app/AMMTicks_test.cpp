#include <test/jtx/AMMTest.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>

#include <xrpld/app/ledger/LedgerMaster.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/CachedView.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <memory>
#include <optional>
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
        env.close();
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
        if (curveType == kCtConcentratedLiquidity)
            jv[sfFeeTier.jsonName] = kFtMedium;
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
        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) -> bool {
            auto const k = keylet::ammTick(ammID, tickIndex);
            auto const sle = std::make_shared<SLE>(k);
            (*sle)[sfAMMID] = ammID;
            sle->setFieldI32(sfTickIndex, tickIndex);
            sle->setFieldU64(sfLiquidityNet, liquidityNet);
            sle->setFieldU64(sfLiquidityGross, liquidityGross);
            sle->setFieldNumber(sfFeeGrowthOutside0, STNumber{sfFeeGrowthOutside0, Number{0}});
            sle->setFieldNumber(sfFeeGrowthOutside1, STNumber{sfFeeGrowthOutside1, Number{0}});
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

        auto const jv =
            ammCreateJV(env, al, usd, eur, usd(1000), eur(1000), kCtConcentratedLiquidity);
        env(jv);
        env.close();

        auto const r = env.rpc(
            "json",
            "amm_ticks",
            to_string(ticksRpcJv(usd, eur, kCtConcentratedLiquidity)))[jss::result];
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

        auto const jv =
            ammCreateJV(env, al, usd, eur, usd(1000), eur(1000), kCtConcentratedLiquidity);
        env(jv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity));
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
            to_string(ticksRpcJv(usd, eur, kCtConcentratedLiquidity)))[jss::result];
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

        auto const jv =
            ammCreateJV(env, al, usd, eur, usd(1000), eur(1000), kCtConcentratedLiquidity);
        env(jv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity));
        BEAST_EXPECT(ammSle != nullptr);
        if (!ammSle)
            return;
        auto const ammID = ammSle->key();

        for (auto const t : {-600, -60, 0, 60, 600})
            insertTick(env, ammID, t, 1000, 2000);

        // Filter to [-100, 100] -> only -60, 0, 60.
        auto const r = env.rpc(
            "json",
            "amm_ticks",
            to_string(ticksRpcJv(usd, eur, kCtConcentratedLiquidity, -100, 100)))[jss::result];
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

        auto const jv =
            ammCreateJV(env, al, usd, eur, usd(1000), eur(1000), kCtConcentratedLiquidity);
        env(jv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity));
        BEAST_EXPECT(ammSle != nullptr);
        if (!ammSle)
            return;
        auto const ammID = ammSle->key();

        std::vector<std::int32_t> const ticks = {-600, -60, 0, 60, 600};
        for (auto const t : ticks)
            insertTick(env, ammID, t, 1, 1);

        // First page: limit 2.
        auto const page1 = env.rpc(
            "json",
            "amm_ticks",
            to_string(ticksRpcJv(
                usd, eur, kCtConcentratedLiquidity, std::nullopt, std::nullopt, 2u)))[jss::result];
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
                kCtConcentratedLiquidity,
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
                kCtConcentratedLiquidity,
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

        auto const jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000), kCtConstantProduct);
        env(jv);
        env.close();

        // curve_type 0 -> should error.
        auto const r = env.rpc(
            "json", "amm_ticks", to_string(ticksRpcJv(usd, eur, kCtConstantProduct)))[jss::result];
        BEAST_EXPECT(r.isMember(jss::error));
    }

    // The error code amm_ticks returns for a request.
    static std::string
    ticksError(jtx::Env& env, json::Value const& jv)
    {
        auto const r = env.rpc("json", "amm_ticks", to_string(jv))[jss::result];
        return r.isMember(jss::error) ? r[jss::error].asString() : std::string{};
    }

    void
    testParameterErrors(FeatureBitset features)
    {
        testcase("amm_ticks - parameter errors");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const al("alice");
        Account const gw2("gateway");
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        fundForAMMCreate(env, gw2, al, usd, eur);
        env(ammCreateJV(env, al, usd, eur, usd(1000), eur(1000), kCtConcentratedLiquidity));
        env(ammCreateJV(env, al, usd, eur, usd(1000), eur(1000), kCtConstantProduct));
        env.close();

        auto const clSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity));
        auto const cpSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtConstantProduct));
        BEAST_EXPECT(clSle && cpSle);
        if (!clSle || !cpSle)
            return;
        auto const clAccount = toBase58(clSle->getAccountID(sfAccount));
        auto const cpAccount = toBase58(cpSle->getAccountID(sfAccount));

        // asset without asset2, under both API versions.
        for (unsigned const api : {1u, 3u})
        {
            auto jv = ticksRpcJv(usd, eur, kCtConcentratedLiquidity);
            jv.removeMember(jss::asset2);
            jv[jss::api_version] = api;
            BEAST_EXPECT(ticksError(env, jv) == "invalidParams");
        }

        // asset and amm_account together.
        {
            auto jv = ticksRpcJv(usd, eur, kCtConcentratedLiquidity);
            jv[jss::amm_account] = clAccount;
            BEAST_EXPECT(ticksError(env, jv) == "invalidParams");
        }

        // Malformed asset and asset2.
        {
            auto jv = ticksRpcJv(usd, eur, kCtConcentratedLiquidity);
            jv[jss::asset] = "not an asset";
            BEAST_EXPECT(!ticksError(env, jv).empty());
            jv = ticksRpcJv(usd, eur, kCtConcentratedLiquidity);
            jv[jss::asset2] = "not an asset";
            BEAST_EXPECT(!ticksError(env, jv).empty());
        }

        // amm_account that is not an address, not in the ledger, or not an AMM.
        {
            json::Value jv;
            jv[jss::amm_account] = "not an address";
            BEAST_EXPECT(ticksError(env, jv) == "actMalformed");
            jv[jss::amm_account] = toBase58(Account("nobody").id());
            BEAST_EXPECT(ticksError(env, jv) == "actMalformed");
            jv[jss::amm_account] = al.human();
            BEAST_EXPECT(ticksError(env, jv) == "actNotFound");
        }

        // amm_account resolves the pool and its curve type.
        {
            json::Value jv;
            jv[jss::amm_account] = clAccount;
            auto const r = env.rpc("json", "amm_ticks", to_string(jv))[jss::result];
            BEAST_EXPECT(!r.isMember(jss::error));
            BEAST_EXPECT(r[jss::amm_id].asString() == to_string(clSle->key()));
            jv[jss::amm_account] = cpAccount;
            BEAST_EXPECT(ticksError(env, jv) == "invalidParams");
            jv[jss::api_version] = 3;
            jv[jss::amm_account] = clAccount;
            BEAST_EXPECT(ticksError(env, jv).empty());
        }

        // asset pair without curve_type, and a curve type with no pool.
        {
            auto jv = ticksRpcJv(usd, eur, kCtConcentratedLiquidity);
            jv.removeMember(jss::curve_type);
            BEAST_EXPECT(ticksError(env, jv) == "invalidParams");
            BEAST_EXPECT(ticksError(env, ticksRpcJv(usd, eur, kCtStableSwap)) == "actNotFound");
        }

        // tick_lower / tick_upper that are not integers or out of range.
        {
            auto jv = ticksRpcJv(usd, eur, kCtConcentratedLiquidity);
            jv[jss::tick_lower] = "low";
            BEAST_EXPECT(ticksError(env, jv) == "invalidParams");
            jv = ticksRpcJv(usd, eur, kCtConcentratedLiquidity);
            jv[jss::tick_upper] = "high";
            BEAST_EXPECT(ticksError(env, jv) == "invalidParams");
            BEAST_EXPECT(
                ticksError(env, ticksRpcJv(usd, eur, kCtConcentratedLiquidity, 10, -10)) ==
                "invalidParams");
            BEAST_EXPECT(
                ticksError(env, ticksRpcJv(usd, eur, kCtConcentratedLiquidity, minTick - 1)) ==
                "invalidParams");
            BEAST_EXPECT(
                ticksError(
                    env,
                    ticksRpcJv(usd, eur, kCtConcentratedLiquidity, std::nullopt, maxTick + 1)) ==
                "invalidParams");
        }

        // limit that is not an integer, and a zero limit that falls back to the default.
        {
            auto jv = ticksRpcJv(usd, eur, kCtConcentratedLiquidity);
            jv[jss::limit] = "ten";
            BEAST_EXPECT(ticksError(env, jv) == "invalidParams");
            BEAST_EXPECT(
                ticksError(
                    env,
                    ticksRpcJv(usd, eur, kCtConcentratedLiquidity, std::nullopt, std::nullopt, 0))
                    .empty());
        }

        // marker that is not a string, not hex, or outside the pool's tick range.
        {
            auto jv = ticksRpcJv(usd, eur, kCtConcentratedLiquidity);
            jv[jss::marker] = 7;
            BEAST_EXPECT(ticksError(env, jv) == "invalidParams");
            BEAST_EXPECT(
                ticksError(
                    env,
                    ticksRpcJv(
                        usd,
                        eur,
                        kCtConcentratedLiquidity,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        "zz")) == "invalidParams");
            BEAST_EXPECT(
                ticksError(
                    env,
                    ticksRpcJv(
                        usd,
                        eur,
                        kCtConcentratedLiquidity,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        to_string(clSle->key()))) == "invalidParams");
        }
    }

    void
    testLedgerEntryByIndex(FeatureBitset features)
    {
        testcase("ledger_entry - AMM tick and tick bitmap by index");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const al("alice");
        Account const gw2("gateway");
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        fundForAMMCreate(env, gw2, al, usd, eur);
        env(ammCreateJV(env, al, usd, eur, usd(1000), eur(1000), kCtConcentratedLiquidity));
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity));
        BEAST_EXPECT(ammSle != nullptr);
        if (!ammSle)
            return;
        auto const ammID = ammSle->key();
        insertTick(env, ammID, 60, 1000, 2000);

        // An ordinary SLE stored at the bitmap word key, readable by index.
        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) -> bool {
            auto const sle = std::make_shared<SLE>(keylet::ammTickBitmapWord(ammID, 3));
            (*sle)[sfAMMID] = ammID;
            sle->setFieldU16(sfBitmapWordIndex, 3);
            sle->setFieldH256(sfBitmapBits, uint256{1u});
            view.rawInsert(sle);
            return true;
        });

        auto const entry = [&](json::StaticString const& field, uint256 const& key) {
            json::Value jv;
            jv[field] = to_string(key);
            jv[jss::ledger_index] = "current";
            return env.rpc("json", "ledger_entry", to_string(jv))[jss::result];
        };

        auto const tick = entry(jss::amm_tick, keylet::ammTick(ammID, 60).key);
        BEAST_EXPECT(tick[jss::node][sfLedgerEntryType.jsonName] == jss::AMMTick);
        auto const word = entry(jss::amm_tick_bitmap, keylet::ammTickBitmapWord(ammID, 3).key);
        BEAST_EXPECT(word[jss::node][sfLedgerEntryType.jsonName] == jss::AMMTickBitmap);

        // A key that holds no entry is not found.
        for (auto const& field :
             {jss::amm_position, jss::amm_bin, jss::amm_bin_holding, jss::amm_tick})
        {
            auto const missing = entry(field, keylet::ammTick(ammID, 61).key);
            BEAST_EXPECT(missing[jss::error] == "entryNotFound");
        }
    }

    void
    testPredecessorKeys(FeatureBitset features)
    {
        testcase("ReadView::pred over AMM tick keys");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const al("alice");
        Account const gw2("gateway");
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        fundForAMMCreate(env, gw2, al, usd, eur);
        env(ammCreateJV(env, al, usd, eur, usd(1000), eur(1000), kCtConcentratedLiquidity));
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity));
        BEAST_EXPECT(ammSle != nullptr);
        if (!ammSle)
            return;
        auto const ammID = ammSle->key();

        // A position over [-60, 60] creates the two ticks in the ledger.
        json::Value dep;
        dep[jss::Account] = al.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfTickLower.jsonName] = -60;
        dep[sfTickUpper.jsonName] = 60;
        dep[jss::Amount] = usd(10).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(10).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep, Ter(tesSUCCESS));
        env.close();

        auto const base = keylet::ammTickBase(ammID).key;
        auto const end = keylet::ammTickEnd(ammID).key;
        auto const lowKey = keylet::ammTick(ammID, -60).key;
        auto const highKey = keylet::ammTick(ammID, 60).key;

        // The closed ledger answers directly.
        auto const closed = env.closed();
        BEAST_EXPECT(closed->pred(end, base) == highKey);
        BEAST_EXPECT(closed->pred(highKey, base) == lowKey);
        BEAST_EXPECT(!closed->pred(lowKey, base));
        BEAST_EXPECT(closed->pred(highKey) == lowKey);

        // An open view and a sandbox over it merge their own changes with the base.
        OpenView open(&*closed);
        open.rawErase(std::make_shared<SLE>(*open.read(keylet::ammTick(highKey))));
        BEAST_EXPECT(open.pred(end, base) == lowKey);
        {
            auto const added = std::make_shared<SLE>(keylet::ammTick(ammID, 120));
            (*added)[sfAMMID] = ammID;
            added->setFieldI32(sfTickIndex, 120);
            added->setFieldU64(sfLiquidityNet, 0);
            added->setFieldU64(sfLiquidityGross, 0);
            added->setFieldNumber(sfFeeGrowthOutside0, STNumber{sfFeeGrowthOutside0, Number{0}});
            added->setFieldNumber(sfFeeGrowthOutside1, STNumber{sfFeeGrowthOutside1, Number{0}});
            open.rawInsert(added);
        }
        auto const addedKey = keylet::ammTick(ammID, 120).key;
        BEAST_EXPECT(open.pred(end, base) == addedKey);
        BEAST_EXPECT(open.pred(addedKey, base) == lowKey);

        Sandbox sb(&open, TapNone);
        sb.erase(sb.peek(keylet::ammTick(addedKey)));
        BEAST_EXPECT(sb.pred(end, base) == lowKey);
        BEAST_EXPECT(!sb.pred(lowKey, base));
        sb.erase(sb.peek(keylet::ammTick(lowKey)));
        BEAST_EXPECT(!sb.pred(end, base));

        CachedView<Ledger> const cached(
            env.app().getLedgerMaster().getClosedLedger(), env.app().getCachedSLEs());
        BEAST_EXPECT(cached.pred(end, base) == highKey);
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
        testParameterErrors(features);
        testLedgerEntryByIndex(features);
        testPredecessorKeys(features);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(AMMTicks, app, xrpl, 1);

}  // namespace xrpl::test
