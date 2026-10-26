#include <test/jtx/AMMTest.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AMMCurve.h>
#include <xrpl/ledger/helpers/AMMTickMath.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <ostream>
#include <string>
#include <utility>

namespace xrpl::test {

// The curve implementations and their ledger helpers, called directly with
// the boundary inputs a payment cannot produce.
struct AMMCurveFunctions_test : public jtx::AMMTestBase
{
private:
    // A funded gateway and LP, the gateway's USD and EUR, and the curve type
    // the pool under test uses.
    struct Pool
    {
        jtx::Account gw{"gateway"};
        jtx::Account lp{"alice"};
        jtx::IOU usd{gw["USD"]};
        jtx::IOU eur{gw["EUR"]};
        UInt256 ammID;
    };

    static void
    fund(jtx::Env& env, Pool const& p)
    {
        using namespace jtx;
        env.fund(XRP(100000), p.gw, p.lp);
        env.close();
        env.trust(p.usd(1000000), p.lp);
        env.trust(p.eur(1000000), p.lp);
        env(pay(p.gw, p.lp, p.usd(100000)));
        env(pay(p.gw, p.lp, p.eur(100000)));
        env.close();
    }

    // Creates the pool and returns its ID.
    static UInt256
    create(jtx::Env& env, Pool const& p, std::uint8_t curveType, json::Value const& params)
    {
        json::Value jv;
        jv[jss::Account] = p.lp.human();
        jv[jss::TransactionType] = jss::AMMCreate;
        jv[jss::Amount] = p.usd(1000).value().getJson(JsonOptions::Values::None);
        jv[jss::Amount2] = p.eur(1000).value().getJson(JsonOptions::Values::None);
        jv[jss::TradingFee] = 0;
        jv[sfCurveType.jsonName] = curveType;
        for (auto const& name : params.getMemberNames())
            jv[name] = params[name];
        jv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(jv, jtx::Ter(tesSUCCESS));
        env.close();
        return env.le(keylet::amm(p.usd.asset(), p.eur.asset(), curveType))->key();
    }

    // A CL position over [lower, upper].
    static void
    depositCL(jtx::Env& env, Pool const& p, std::int32_t lower, std::int32_t upper)
    {
        json::Value dep;
        dep[jss::Account] = p.lp.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, p.usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, p.eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfTickLower.jsonName] = lower;
        dep[sfTickUpper.jsonName] = upper;
        dep[jss::Amount] = p.usd(100).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = p.eur(100).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep, jtx::Ter(tesSUCCESS));
        env.close();
    }

    // A tick SLE with the given net liquidity, written straight to `view`.
    static void
    writeTick(OpenView& view, UInt256 const& ammID, std::int32_t tick, std::uint64_t net)
    {
        auto const sle = std::make_shared<SLE>(keylet::ammTick(ammID, tick));
        (*sle)[sfAMMID] = ammID;
        sle->setFieldI32(sfTickIndex, tick);
        sle->setFieldU64(sfLiquidityNet, net);
        sle->setFieldU64(sfLiquidityGross, 1);
        sle->setFieldNumber(sfFeeGrowthOutside0, STNumber{sfFeeGrowthOutside0, Number{0}});
        sle->setFieldNumber(sfFeeGrowthOutside1, STNumber{sfFeeGrowthOutside1, Number{0}});
        sle->setFieldU64(sfOwnerNode, 0);
        if (view.read(keylet::ammTick(ammID, tick)))
        {
            view.rawReplace(sle);
        }
        else
        {
            view.rawInsert(sle);
        }
    }

    void
    testConstantProduct()
    {
        testcase("constant product: empty pool, zero amounts and the invariant");
        using namespace jtx;
        Pool const p;
        auto const* curve = getCurve(kCtConstantProduct);
        auto const& usd = p.usd;
        auto const& eur = p.eur;

        BEAST_EXPECT(curve->swapIn(usd(0), eur(0), usd(0), 0, nullptr).error() == tecAMM_FAILED);
        BEAST_EXPECT(
            curve->swapIn(usd(100), eur(100), usd(0), 0, nullptr).error() == tecAMM_FAILED);
        BEAST_EXPECT(
            curve->swapOut(usd(100), eur(100), eur(100), 0, nullptr).error() == tecAMM_FAILED);
        BEAST_EXPECT(
            curve->swapOut(usd(100), eur(100), eur(0), 0, nullptr).error() == tecAMM_FAILED);
        BEAST_EXPECT(curve->checkInvariant(usd(100), eur(100), usd(100), eur(100), nullptr));
        BEAST_EXPECT(!curve->checkInvariant(usd(100), eur(100), usd(100), eur(90), nullptr));
    }

    void
    testStableSwap()
    {
        testcase("stable swap: missing pool entry, zero and oversized amounts");
        using namespace jtx;
        Pool const p;
        auto const* curve = getCurve(kCtStableSwap);
        auto const& usd = p.usd;
        auto const& eur = p.eur;
        STObject params(sfGeneric);
        params.setFieldU32(sfAmplification, 100);

        BEAST_EXPECT(curve->swapIn(usd(100), eur(100), usd(1), 0, nullptr).error() == tecINTERNAL);
        BEAST_EXPECT(curve->swapOut(usd(100), eur(100), eur(1), 0, nullptr).error() == tecINTERNAL);
        BEAST_EXPECT(curve->spotPrice(usd(100), eur(100), 0, nullptr).error() == tecINTERNAL);
        BEAST_EXPECT(
            curve->initialLPTokens(usd(100), eur(100), Issue{}, nullptr).error() == tecINTERNAL);
        BEAST_EXPECT(!curve->checkInvariant(usd(100), eur(100), usd(100), eur(100), nullptr));

        BEAST_EXPECT(
            curve->swapIn(usd(100), eur(100), usd(0), 0, &params).error() == tecAMM_FAILED);
        BEAST_EXPECT(
            curve->swapOut(usd(100), eur(100), eur(100), 0, &params).error() == tecAMM_FAILED);
        BEAST_EXPECT(
            curve->swapOut(usd(100), eur(100), eur(0), 0, &params).error() == tecAMM_FAILED);
        BEAST_EXPECT(curve->checkInvariant(usd(100), eur(100), usd(100), eur(100), &params));
        BEAST_EXPECT(!curve->checkInvariant(usd(100), eur(100), usd(100), eur(50), &params));

        STObject noAmp(sfGeneric);
        BEAST_EXPECT(curve->validateParams(noAmp) == temMALFORMED);
        noAmp.setFieldU32(sfAmplification, maxAmplification + 1);
        BEAST_EXPECT(curve->validateParams(noAmp) == temMALFORMED);
        BEAST_EXPECT(curve->validateParams(params) == tesSUCCESS);

        // An empty pool mints no LP tokens.
        auto const emptyLP = curve->initialLPTokens(usd(0), eur(0), Issue{}, &params);
        BEAST_EXPECT(emptyLP && *emptyLP == beast::kZero);
    }

    void
    testConcentratedLiquidityWithoutView()
    {
        testcase("concentrated liquidity: single-range math, spot price and the invariant");
        using namespace jtx;
        Pool const p;
        auto const* curve = getCurve(kCtConcentratedLiquidity);
        auto const& usd = p.usd;
        auto const& eur = p.eur;
        STObject sle(sfGeneric);
        sle.setFieldU64(sfActiveLiquidity, 1000);
        sle.setFieldI32(sfCurrentTick, 0);

        BEAST_EXPECT(curve->swapIn(usd(100), eur(100), usd(1), 0, nullptr).error() == tecINTERNAL);
        BEAST_EXPECT(curve->swapOut(usd(100), eur(100), eur(1), 0, nullptr).error() == tecINTERNAL);
        BEAST_EXPECT(curve->spotPrice(usd(100), eur(100), 0, nullptr).error() == tecINTERNAL);

        // Zero input yields no output; more output than the range holds fails.
        BEAST_EXPECT(curve->swapIn(usd(100), eur(100), usd(0), 0, &sle).error() == tecAMM_FAILED);
        BEAST_EXPECT(
            curve->swapOut(usd(100), eur(100), eur(5000), 0, &sle).error() == tecAMM_FAILED);
        BEAST_EXPECT(
            curve->swapOut(eur(100), usd(100), usd(5000), 0, &sle).error() == tecAMM_FAILED);

        // At tick 0 the price is one in both directions, before the fee.
        auto const forward = curve->spotPrice(usd(100), eur(100), 0, &sle);
        auto const backward = curve->spotPrice(eur(100), usd(100), 0, &sle);
        BEAST_EXPECT(forward && backward && *forward == Number{1} && *backward == Number{1});
        auto const withFee = curve->spotPrice(usd(100), eur(100), 1000, &sle);
        BEAST_EXPECT(withFee && *withFee > Number{1});

        // No change, in up with out down, and out down alone.
        BEAST_EXPECT(curve->checkInvariant(usd(100), eur(100), usd(100), eur(100), &sle));
        BEAST_EXPECT(curve->checkInvariant(usd(100), eur(100), usd(110), eur(90), &sle));
        BEAST_EXPECT(!curve->checkInvariant(usd(100), eur(100), usd(100), eur(90), &sle));
        BEAST_EXPECT(!curve->checkInvariant(usd(100), eur(100), usd(110), eur(100), &sle));

        // Step amounts carry no asset; a swap on them takes its direction from
        // the pool's assets and matches the swap on the pool's amounts.
        STObject shifted(sfGeneric);
        shifted.setFieldU64(sfActiveLiquidity, 100000);
        shifted.setFieldI32(sfCurrentTick, 1000);
        bool const usdIsToken0 = usd.asset() < eur.asset();
        auto const& token0 = usdIsToken0 ? usd : eur;
        auto const& token1 = usdIsToken0 ? eur : usd;
        TAmounts<IOUAmount, IOUAmount> const pool{IOUAmount{100, 0}, IOUAmount{100, 0}};
        for (bool const zeroForOne : {true, false})
        {
            auto const& in = zeroForOne ? token0 : token1;
            auto const& out = zeroForOne ? token1 : token0;
            auto const stepOut = curveSwapIn(
                pool,
                in.asset(),
                out.asset(),
                IOUAmount{1, 0},
                0,
                kCtConcentratedLiquidity,
                &shifted);
            auto const directOut = curve->swapIn(in(100), out(100), in(1), 0, &shifted);
            BEAST_EXPECT(directOut && toSTAmount(stepOut, out.asset()) == *directOut);
            auto const stepIn = curveSwapOut(
                pool,
                in.asset(),
                out.asset(),
                IOUAmount{1, 0},
                0,
                kCtConcentratedLiquidity,
                &shifted);
            auto const directIn = curve->swapOut(in(100), out(100), out(1), 0, &shifted);
            BEAST_EXPECT(directIn && toSTAmount(stepIn, in.asset()) == *directIn);

            // A swap the curve refuses quotes no output, or the largest input.
            IOUAmount const zero{0, 0};
            auto const noOut = curveSwapIn(
                pool, in.asset(), out.asset(), zero, 0, kCtConcentratedLiquidity, &shifted);
            BEAST_EXPECT(noOut == zero);
            auto const maxIn = curveSwapOut(
                pool,
                in.asset(),
                out.asset(),
                IOUAmount{1, 9},
                0,
                kCtConcentratedLiquidity,
                &shifted);
            BEAST_EXPECT(maxIn == toMaxAmount<IOUAmount>(in.asset()));
        }
    }

    void
    testConcentratedLiquidityWalks(FeatureBitset features)
    {
        testcase("concentrated liquidity: walks that run out of ticks or liquidity");
        using namespace jtx;
        Env env(*this, features | featureAMMCurves);
        Pool p;
        fund(env, p);
        json::Value params;
        params[sfFeeTier.jsonName] = kFtMedium;
        p.ammID = create(env, p, kCtConcentratedLiquidity, params);
        depositCL(env, p, -600, 600);

        auto const* curve = getCurve(kCtConcentratedLiquidity);
        auto const ammSle = env.le(keylet::amm(p.ammID));
        CurveContext const cctx{.view = &*env.current(), .ammID = &p.ammID};
        bool const usdIsToken0 = p.usd.asset() < p.eur.asset();
        auto const& token0 = usdIsToken0 ? p.usd : p.eur;
        auto const& token1 = usdIsToken0 ? p.eur : p.usd;

        // A large swap leaves the only range: liquidity drops to zero at its
        // edge and the walk stops there, in both directions; a swap out the
        // range cannot fill fails.
        for (bool const zeroForOne : {true, false})
        {
            auto const& in = zeroForOne ? token0 : token1;
            auto const& out = zeroForOne ? token1 : token0;
            auto const swapped = curve->swapIn(in(100), out(100), in(100000), 0, &*ammSle, cctx);
            BEAST_EXPECT(swapped && *swapped > out(0) && *swapped < out(101));
            auto const quoted = curve->swapOut(in(100), out(100), out(1000), 0, &*ammSle, cctx);
            BEAST_EXPECT(quoted.error() == tecAMM_FAILED);
        }

        // A pool priced at the top tick has no tick left to swap towards.
        {
            STObject lone(*ammSle);
            lone.setFieldI32(sfCurrentTick, maxTick);
            lone.setFieldNumber(sfSqrtPrice, STNumber{sfSqrtPrice, tickToSqrtPrice(maxTick)});
            BEAST_EXPECT(
                curve->swapIn(token1(100), token0(100), token1(1), 0, &lone, cctx).error() ==
                tecAMM_FAILED);
            BEAST_EXPECT(
                curve->swapOut(token1(100), token0(100), token0(1), 0, &lone, cctx).error() ==
                tecAMM_FAILED);
        }

        // Running liquidity past int64, or a crossing that would take it there,
        // ends the walk at that tick with the output so far.
        {
            auto constexpr int64Max = std::numeric_limits<std::int64_t>::max();
            STObject past(*ammSle);
            past.setFieldU64(sfActiveLiquidity, static_cast<std::uint64_t>(int64Max) + 1);
            auto const down = curve->swapIn(
                token0(100),
                token1(100),
                token0(std::int64_t{1000000000000000000}),
                0,
                &past,
                cctx);
            BEAST_EXPECT(down && *down > token1(0));

            STObject below(*ammSle);
            below.setFieldU64(sfActiveLiquidity, static_cast<std::uint64_t>(int64Max));
            below.setFieldI32(sfCurrentTick, -700);
            below.setFieldNumber(sfSqrtPrice, STNumber{sfSqrtPrice, tickToSqrtPrice(-700)});
            auto const up = curve->swapIn(
                token1(100),
                token0(100),
                token1(std::int64_t{100000000000000000}),
                0,
                &below,
                cctx);
            BEAST_EXPECT(up && *up > token0(0));
        }

        // A tick whose net liquidity would drive the running liquidity below
        // zero ends the walk at that tick, short of a large swap out.
        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) {
            writeTick(view, p.ammID, 600, static_cast<std::uint64_t>(-1'000'000'000'000));
            return true;
        });
        {
            CurveContext const open{.view = &*env.current(), .ammID = &p.ammID};
            auto const sle = env.current()->read(keylet::amm(p.ammID));
            auto const in = curve->swapIn(token1(100), token0(100), token1(100000), 0, &*sle, open);
            BEAST_EXPECT(in.has_value());
            auto const out = curve->swapOut(token1(100), token0(100), token0(1000), 0, &*sle, open);
            BEAST_EXPECT(out.error() == tecAMM_FAILED);

            // applySwap stops at that tick too, keeping the price on it.
            OpenView ov(&*env.current());
            Sandbox sb(&ov, TapNone);
            BEAST_EXPECT(
                isTesSuccess(curve->applySwap(sb, p.ammID, token1(100000), token0(1), 0, &*sle)));
            BEAST_EXPECT(clSqrtPrice(*sb.read(keylet::amm(p.ammID))) == tickToSqrtPrice(600));
        }
    }

    // The sum of the net liquidity of the given initialised ticks at or below `tick`:
    // the liquidity of the positions whose range holds `tick`.
    static std::int64_t
    netAtOrBelow(
        ReadView const& view,
        UInt256 const& ammID,
        std::int32_t tick,
        std::initializer_list<std::int32_t> ticks)
    {
        std::int64_t sum = 0;
        for (auto const t : ticks)
        {
            if (t <= tick)
            {
                sum += static_cast<std::int64_t>(
                    view.read(keylet::ammTick(ammID, t))->getFieldU64(sfLiquidityNet));
            }
        }
        return sum;
    }

    void
    testRoundTripQuotes()
    {
        testcase("stable swap and binned: a quoted round trip never returns more than went in");
        using namespace jtx;
        Pool const p;
        auto const& usd = p.usd;
        auto const& eur = p.eur;
        STObject ss(sfGeneric);
        ss.setFieldU32(sfAmplification, 100);
        STObject binned(sfGeneric);
        binned.setFieldU16(sfBinStep, 25);
        binned.setFieldI32(sfActiveBinID, 3);
        for (auto const& [curveType, params] :
             {std::pair{kCtStableSwap, &ss}, std::pair{kCtBinned, &binned}})
        {
            auto const* curve = getCurve(curveType);
            for (STAmount const& in : {
                     usd(1).value(),
                     STAmount{usd.issue(), 1, -6},
                     STAmount{usd.issue(), 123456789, -6},
                 })
            {
                STAmount const poolUsd = usd(1000).value();
                STAmount const poolEur = eur(997).value();
                auto const out = curve->swapIn(poolUsd, poolEur, in, 0, params);
                if (!BEAST_EXPECT(out))
                    continue;
                auto const back = curve->swapIn(poolEur - *out, poolUsd + in, *out, 0, params);
                BEAST_EXPECT(back && *back <= in);
            }
        }
    }

    void
    testBoundaryInputs(FeatureBitset features)
    {
        testcase(
            "concentrated liquidity: an input just short of a boundary keeps liquidity and tick in "
            "step");
        using namespace jtx;
        Env env(*this, features | featureAMMCurves);
        Pool p;
        fund(env, p);
        json::Value params;
        params[sfFeeTier.jsonName] = kFtStable;
        p.ammID = create(env, p, kCtConcentratedLiquidity, params);
        depositCL(env, p, -600, 600);
        depositCL(env, p, -1, 1);
        auto const* curve = getCurve(kCtConcentratedLiquidity);
        bool const usdIsToken0 = p.usd.asset() < p.eur.asset();
        auto const& token0 = usdIsToken0 ? p.usd : p.eur;
        auto const& token1 = usdIsToken0 ? p.eur : p.usd;

        // A swap runs under whatever rounding mode its caller set.
        for (auto const mode : {Number::RoundingMode::Upward, Number::RoundingMode::Downward})
        {
            for (bool const zeroForOne : {true, false})
            {
                auto const& in = zeroForOne ? token0 : token1;
                auto const& out = zeroForOne ? token1 : token0;
                Number const target = tickToSqrtPrice(zeroForOne ? -1 : 1);
                for (int k = 1; k <= 256; ++k)
                {
                    OpenView ov(&*env.current());
                    Sandbox sb(&ov, TapNone);
                    auto const amm = sb.read(keylet::amm(p.ammID));
                    Number const l{static_cast<std::int64_t>(amm->getFieldU64(sfActiveLiquidity))};
                    Number const s0 = clSqrtPrice(*amm);
                    Number const toTarget =
                        zeroForOne ? l * (s0 - target) / (s0 * target) : l * (target - s0);
                    Number const shortOfTarget = toTarget * (Number{1} - Number{k, -17});
                    auto const input =
                        toSTAmount(in.asset(), shortOfTarget, Number::RoundingMode::Upward);
                    {
                        NumberRoundModeGuard const mg(mode);
                        BEAST_EXPECT(
                            isTesSuccess(curve->applySwap(sb, p.ammID, input, out(1), 0, &*amm)));
                    }
                    auto const after = sb.read(keylet::amm(p.ammID));
                    auto const tick = after->getFieldI32(sfCurrentTick);
                    BEAST_EXPECT(
                        static_cast<std::int64_t>(after->getFieldU64(sfActiveLiquidity)) ==
                        netAtOrBelow(sb, p.ammID, tick, {-600, -1, 1, 600}));
                }
            }
        }
    }

    void
    testConcentratedLiquidityApplySwap(FeatureBitset features)
    {
        testcase("concentrated liquidity: applySwap preconditions and the crossing cap");
        using namespace jtx;
        Env env(*this, features | featureAMMCurves);
        Pool p;
        fund(env, p);
        json::Value params;
        params[sfFeeTier.jsonName] = kFtStable;
        p.ammID = create(env, p, kCtConcentratedLiquidity, params);

        auto const* curve = getCurve(kCtConcentratedLiquidity);
        bool const usdIsToken0 = p.usd.asset() < p.eur.asset();
        auto const& token0 = usdIsToken0 ? p.usd : p.eur;
        auto const& token1 = usdIsToken0 ? p.eur : p.usd;

        // No pool at the ID, and a pool with no active liquidity.
        {
            OpenView ov(&*env.current());
            Sandbox sb(&ov, TapNone);
            UInt256 missing;
            missing.data()[0] = 9;
            BEAST_EXPECT(
                curve->applySwap(sb, missing, token0(1), token1(1), 0, nullptr) == tecINTERNAL);
            auto const sle = env.le(keylet::amm(p.ammID));
            BEAST_EXPECT(
                curve->applySwap(sb, p.ammID, token0(1), token1(1), 0, &*sle) == tecINTERNAL);
        }

        depositCL(env, p, -2000, 2000);

        // A zero input has nothing to apply.
        {
            OpenView ov(&*env.current());
            Sandbox sb(&ov, TapNone);
            auto const sle = env.le(keylet::amm(p.ammID));
            BEAST_EXPECT(
                curve->applySwap(sb, p.ammID, token0(0), token1(0), 0, &*sle) == tecINTERNAL);
        }

        // More than maxTickCrossings ticks above the current price stop the
        // walk with tecAMM_TICK_CAP_HIT.
        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) {
            Sandbox sb(&view, TapNone);
            for (std::int32_t t = 1; t <= maxTickCrossings + 5; ++t)
                BEAST_EXPECT(isTesSuccess(setTickBitmap(sb, p.ammID, t, env.journal)));
            sb.apply(view);
            for (std::int32_t t = 1; t <= maxTickCrossings + 5; ++t)
                writeTick(view, p.ammID, t, 0);
            return true;
        });
        {
            OpenView ov(&*env.current());
            Sandbox sb(&ov, TapNone);
            auto const sle = env.current()->read(keylet::amm(p.ammID));
            BEAST_EXPECT(
                curve->applySwap(sb, p.ammID, token1(500), token0(1), 0, &*sle) ==
                tecAMM_TICK_CAP_HIT);
        }

        // A swap out that would cross more ticks than the cap cannot deliver its output.
        {
            auto const sle = env.current()->read(keylet::amm(p.ammID));
            bool capHit = false;
            CurveContext const capped{
                .view = &*env.current(),
                .ammID = &p.ammID,
                .tickCapHit = &capHit,
            };
            BEAST_EXPECT(
                curve->swapOut(token1(100000), token0(100000), token0(500), 0, &*sle, capped)
                    .error() == tecAMM_FAILED);
            BEAST_EXPECT(capHit);
        }

        // A pool priced at the lowest tick has nothing left to sell downwards.
        {
            OpenView ov(&*env.current());
            Sandbox sb(&ov, TapNone);
            auto const amm = sb.peek(keylet::amm(p.ammID));
            amm->setFieldI32(sfCurrentTick, minTick);
            amm->setFieldNumber(sfSqrtPrice, STNumber{sfSqrtPrice, tickToSqrtPrice(minTick)});
            sb.update(amm);
            BEAST_EXPECT(
                curve->applySwap(sb, p.ammID, token0(1), token1(1), 0, &*amm) == tecINTERNAL);
        }

        // Fee growth inside a range needs both of its tick entries.
        BEAST_EXPECT(!clFeeGrowthInside(*env.current(), *env.le(keylet::amm(p.ammID)), -5, 5));
    }

    void
    testBinned(FeatureBitset features)
    {
        testcase("binned: missing pool entry, fallbacks and applySwap without liquidity");
        using namespace jtx;
        Pool const q;
        auto const* curve = getCurve(kCtBinned);
        auto const& usd = q.usd;
        auto const& eur = q.eur;
        bool const usdIsToken0 = usd.asset() < eur.asset();
        auto const& token0 = usdIsToken0 ? usd : eur;
        auto const& token1 = usdIsToken0 ? eur : usd;
        STObject params(sfGeneric);
        params.setFieldU16(sfBinStep, 25);
        params.setFieldI32(sfActiveBinID, 0);

        BEAST_EXPECT(
            curve->swapIn(token0(100), token1(100), token0(1), 0, nullptr).error() == tecINTERNAL);
        BEAST_EXPECT(
            curve->swapOut(token0(100), token1(100), token1(1), 0, nullptr).error() == tecINTERNAL);
        BEAST_EXPECT(curve->spotPrice(token0(1), token1(1), 0, nullptr).error() == tecINTERNAL);
        BEAST_EXPECT(!curve->checkInvariant(token0(1), token1(1), token0(1), token1(1), nullptr));

        // Without a view: the output is capped at the pool, zero amounts fail,
        // and more output than the pool holds fails.
        auto const capped = curve->swapIn(token0(100), token1(100), token0(1000), 0, &params);
        BEAST_EXPECT(capped && *capped == token1(100));
        BEAST_EXPECT(
            curve->swapIn(token0(100), token1(100), token0(0), 0, &params).error() ==
            tecAMM_FAILED);
        BEAST_EXPECT(
            curve->swapOut(token0(100), token1(100), token1(0), 0, &params).error() ==
            tecAMM_FAILED);
        BEAST_EXPECT(
            curve->swapOut(token0(100), token1(100), token1(101), 0, &params).error() ==
            tecAMM_FAILED);
        BEAST_EXPECT(curve->swapOut(token1(100), token0(100), token0(10), 0, &params).has_value());

        // The constant-sum invariant at the bin price, both directions.
        BEAST_EXPECT(
            curve->checkInvariant(token0(100), token1(100), token0(110), token1(90), &params));
        BEAST_EXPECT(
            !curve->checkInvariant(token0(100), token1(100), token0(100), token1(50), &params));
        BEAST_EXPECT(
            curve->checkInvariant(token1(100), token0(100), token1(110), token0(90), &params));

        Env env(*this, features | featureAMMCurves);
        Pool p;
        fund(env, p);
        json::Value createParams;
        createParams[sfBinStep.jsonName] = 25u;
        p.ammID = create(env, p, kCtBinned, createParams);
        auto const sle = env.le(keylet::amm(p.ammID));

        // applySwap needs the pool entry and, with no bin liquidity, the
        // active bin.
        {
            OpenView ov(&*env.current());
            Sandbox sb(&ov, TapNone);
            BEAST_EXPECT(
                curve->applySwap(sb, p.ammID, token0(1), token1(1), 0, nullptr) == tecINTERNAL);
            BEAST_EXPECT(
                curve->applySwap(sb, p.ammID, token0(1), token1(1), 0, &*sle) == tecINTERNAL);
        }

        // An empty active bin takes the swap directly, in either direction.
        json::Value bin;
        bin[jss::Account] = p.lp.human();
        bin[jss::TransactionType] = "AMMBinCreate";
        bin[jss::Asset] = STIssue(sfAsset, p.usd.asset()).getJson(JsonOptions::Values::None);
        bin[jss::Asset2] = STIssue(sfAsset, p.eur.asset()).getJson(JsonOptions::Values::None);
        bin[sfBinID.jsonName] = 0;
        bin[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(bin, Ter(tesSUCCESS));
        env.close();
        for (bool const in0 : {true, false})
        {
            OpenView ov(&*env.current());
            Sandbox sb(&ov, TapNone);
            auto const& in = in0 ? token0 : token1;
            auto const& out = in0 ? token1 : token0;
            BEAST_EXPECT(isTesSuccess(curve->applySwap(sb, p.ammID, in(1), out(0), 0, &*sle)));
            auto const binSle = sb.read(keylet::ammBin(p.ammID, 0));
            BEAST_EXPECT(binSle->getFieldAmount(in0 ? sfReserve0 : sfReserve1) == in(1));
        }

        // With a view: a zero input walks no bin, and bins with no output reserve give no output.
        {
            CurveContext const cctx{.view = &*env.current(), .ammID = &p.ammID};
            BEAST_EXPECT(
                curve->swapIn(token0(100), token1(100), token0(0), 0, &*sle, cctx).error() ==
                tecAMM_FAILED);
            BEAST_EXPECT(
                curve->swapIn(token0(100), token1(100), token0(1), 0, &*sle, cctx).error() ==
                tecAMM_FAILED);
        }

        // The active bin helper: no active bin field, and an active bin with
        // no entry.
        {
            STObject const noActive(sfGeneric);
            BEAST_EXPECT(!maxBinnedOutputAtActiveBin(*env.current(), p.ammID, noActive, true));
            STObject elsewhere(sfGeneric);
            elsewhere.setFieldI32(sfActiveBinID, 5);
            BEAST_EXPECT(!maxBinnedOutputAtActiveBin(*env.current(), p.ammID, elsewhere, true));
        }
    }

    void
    testTickBitmap(FeatureBitset features)
    {
        testcase("tick bitmap: repeated set, clear of an absent word or bit, partial clear");
        using namespace jtx;
        Env const env(*this, features | featureAMMCurves);
        UInt256 ammID;
        ammID.data()[0] = 3;
        OpenView ov(&*env.current());
        Sandbox sb(&ov, TapNone);
        auto const word = [&](std::int32_t tick) {
            return sb.read(keylet::ammTickBitmapWord(ammID, tickToBitmapPos(tick).first));
        };

        BEAST_EXPECT(isTesSuccess(clearTickBitmap(sb, ammID, 5, env.journal)));
        BEAST_EXPECT(!word(5));

        BEAST_EXPECT(isTesSuccess(setTickBitmap(sb, ammID, 5, env.journal)));
        BEAST_EXPECT(isTesSuccess(setTickBitmap(sb, ammID, 5, env.journal)));
        BEAST_EXPECT(isTesSuccess(setTickBitmap(sb, ammID, 6, env.journal)));
        BEAST_EXPECT(isTesSuccess(clearTickBitmap(sb, ammID, 7, env.journal)));

        // Clearing one of two bits keeps the word; clearing the last removes it.
        BEAST_EXPECT(isTesSuccess(clearTickBitmap(sb, ammID, 5, env.journal)));
        BEAST_EXPECT(
            word(6) &&
            bitmapBitIsSet(word(6)->getFieldH256(sfBitmapBits), tickToBitmapPos(6).second));
        BEAST_EXPECT(
            !bitmapBitIsSet(word(5)->getFieldH256(sfBitmapBits), tickToBitmapPos(5).second));
        BEAST_EXPECT(isTesSuccess(clearTickBitmap(sb, ammID, 6, env.journal)));
        BEAST_EXPECT(!word(6));
    }

    void
    testRangeCap(FeatureBitset features)
    {
        testcase("concentrated liquidity: the within-range output cap");
        using namespace jtx;
        Env env(*this, features | featureAMMCurves);
        Pool p;
        fund(env, p);
        json::Value params;
        params[sfFeeTier.jsonName] = kFtMedium;
        p.ammID = create(env, p, kCtConcentratedLiquidity, params);

        auto const view = env.current();
        STObject const missing(sfGeneric);
        BEAST_EXPECT(!clCurrentRangeCapacity(*view, p.ammID, missing, true));

        STObject empty(sfGeneric);
        empty.setFieldU64(sfActiveLiquidity, 0);
        empty.setFieldI32(sfCurrentTick, 0);
        auto const zero = clCurrentRangeCapacity(*view, p.ammID, empty, true);
        BEAST_EXPECT(zero && zero->out == Number{0} && zero->in == Number{0});

        // Liquidity priced at the bottom tick has no room to move down.
        STObject lone(sfGeneric);
        lone.setFieldU64(sfActiveLiquidity, 1000);
        lone.setFieldI32(sfCurrentTick, minTick);
        auto const floor = clCurrentRangeCapacity(*view, p.ammID, lone, true);
        BEAST_EXPECT(floor && floor->out == Number{0} && floor->in == Number{0});

        depositCL(env, p, -600, 600);
        auto const after = env.current();
        auto const sle = after->read(keylet::amm(p.ammID));
        for (bool const zeroForOne : {true, false})
        {
            auto const cap = clCurrentRangeCapacity(*after, p.ammID, *sle, zeroForOne);
            BEAST_EXPECT(cap && cap->out > Number{0} && cap->in > Number{0});
        }
    }

public:
    // Places the CL state of `ammID` in `sb`: liquidity `inner` over [-2000, 2000] and `outer`
    // over [-4000, 4000], the price at `sqrtP` and the tick holding it.
    static void
    placeTwoRanges(
        Sandbox& sb,
        UInt256 const& ammID,
        std::int64_t inner,
        std::int64_t outer,
        Number const& sqrtP)
    {
        for (auto const& [tick, net] : {
                 std::pair{-4000, outer},
                 std::pair{-2000, inner},
                 std::pair{2000, -inner},
                 std::pair{4000, -outer},
             })
        {
            auto const sle = sb.peek(keylet::ammTick(ammID, tick));
            sle->setFieldU64(sfLiquidityNet, static_cast<std::uint64_t>(net));
            sb.update(sle);
        }
        auto const amm = sb.peek(keylet::amm(ammID));
        amm->setFieldU64(sfActiveLiquidity, static_cast<std::uint64_t>(inner + outer));
        amm->setFieldNumber(sfSqrtPrice, STNumber{sfSqrtPrice, sqrtP});
        amm->setFieldI32(sfCurrentTick, sqrtPriceToTick(sqrtP));
        sb.update(amm);
    }

    void
    testDownwardSegmentStopsOnTick(FeatureBitset features)
    {
        testcase("concentrated liquidity: a downward swap short of a tick stays on or above it");
        using namespace jtx;
        Env env(*this, features | featureAMMCurves);
        Pool p;
        fund(env, p);
        json::Value params;
        params[sfFeeTier.jsonName] = kFtStable;
        p.ammID = create(env, p, kCtConcentratedLiquidity, params);
        depositCL(env, p, -4000, 4000);
        depositCL(env, p, -2000, 2000);
        auto const* curve = getCurve(kCtConcentratedLiquidity);
        bool const usdIsToken0 = p.usd.asset() < p.eur.asset();
        auto const& token0 = usdIsToken0 ? p.usd : p.eur;
        auto const& token1 = usdIsToken0 ? p.eur : p.usd;

        NumberMantissaScaleGuard const scale{MantissaRange::MantissaScale::Large};
        std::int32_t constexpr target = -2000;
        Number const sqrtTarget = tickToSqrtPrice(target);
        // Inputs whose fee-adjusted amount falls a few units of the last place short of the
        // input that reaches tick -2000, from prices in the range above it (the same bitmap
        // word, so the walk's next tick is -2000).
        int ending = 0;
        int pastTarget = 0;
        for (std::int64_t const l : {1'000'000'000LL, 1'000'000'000'000LL, 1'000'000'000'000'000LL})
        {
            for (std::int32_t const startTick : {-1999, -1990, -1950, -1900, -1800})
            {
                for (int off = 0; off < 6; ++off)
                {
                    for (std::uint16_t const tfee : {0, 10, 50, 300, 1000})
                    {
                        for (int const k : {0, 1, 2, 4, 8})
                        {
                            OpenView ov(&*env.current());
                            Sandbox sb(&ov, TapNone);
                            Number const start =
                                tickToSqrtPrice(startTick) * (Number{1} - Number{off * 7, -7});
                            placeTwoRanges(sb, p.ammID, (l / 5) * 3, l - ((l / 5) * 3), start);
                            auto const amm = sb.read(keylet::amm(p.ammID));
                            Number const s0 = clSqrtPrice(*amm);
                            Number const lNum{l};
                            Number const toTarget = lNum * (s0 - sqrtTarget) / (s0 * sqrtTarget);
                            Number const f = feeMult(tfee);
                            STAmount const reach = toSTAmount(
                                token0.asset(), toTarget / f, Number::RoundingMode::Downward);
                            STAmount const input{
                                token0.asset(),
                                reach.mantissa() - static_cast<std::uint64_t>(k),
                                reach.exponent()};
                            Number const in = Number{input} * f;
                            if (!(in < toTarget))
                                continue;
                            ++ending;
                            if ((lNum * s0) / (lNum + in * s0) < sqrtTarget)
                                ++pastTarget;

                            BEAST_EXPECT(isTesSuccess(
                                curve->applySwap(sb, p.ammID, input, token1(1), tfee, &*amm)));
                            auto const down = sb.read(keylet::amm(p.ammID));
                            BEAST_EXPECT(clSqrtPrice(*down) >= sqrtTarget);
                            BEAST_EXPECT(down->getFieldI32(sfCurrentTick) >= target);

                            // A swap back up keeps the active liquidity that of the
                            // positions holding the tick it ends in.
                            BEAST_EXPECT(isTesSuccess(
                                curve->applySwap(sb, p.ammID, token1(1), token0(1), tfee, &*down)));
                            auto const up = sb.read(keylet::amm(p.ammID));
                            BEAST_EXPECT(
                                static_cast<std::int64_t>(up->getFieldU64(sfActiveLiquidity)) ==
                                netAtOrBelow(
                                    sb,
                                    p.ammID,
                                    up->getFieldI32(sfCurrentTick),
                                    {-4000, -2000, 2000, 4000}));
                        }
                    }
                }
            }
        }
        // The sweep reaches inputs whose unbounded price lands below tick -2000.
        BEAST_EXPECT(ending > 0 && pastTarget > 0);
        log << "downward segments " << ending << ", unbounded past the tick " << pastTarget
            << std::endl;

        // l = 1e9 from tick -1999 with no fee: an input of 55254.12560527307 is short of the
        // 55254.12560527307936 that reaches tick -2000, yet its unbounded price
        // 0.9048419419327688724 is below tick -2000's 0.9048419419327688725.
        {
            OpenView ov(&*env.current());
            Sandbox sb(&ov, TapNone);
            placeTwoRanges(sb, p.ammID, 600'000'000, 400'000'000, tickToSqrtPrice(-1999));
            auto const amm = sb.read(keylet::amm(p.ammID));
            STAmount const input{token0.asset(), std::uint64_t{5525412560527307}, -11};
            BEAST_EXPECT(isTesSuccess(curve->applySwap(sb, p.ammID, input, token1(1), 0, &*amm)));
            auto const down = sb.read(keylet::amm(p.ammID));
            BEAST_EXPECT(clSqrtPrice(*down) == sqrtTarget);
            BEAST_EXPECT(down->getFieldI32(sfCurrentTick) == target);
            BEAST_EXPECT(down->getFieldU64(sfActiveLiquidity) == 1'000'000'000);
        }
    }

    void
    run() override
    {
        auto const features = jtx::testableAmendments();
        testConstantProduct();
        testStableSwap();
        testConcentratedLiquidityWithoutView();
        testConcentratedLiquidityWalks(features);
        testConcentratedLiquidityApplySwap(features);
        testBinned(features);
        testTickBitmap(features);
        testRangeCap(features);
        testBoundaryInputs(features);
        testRoundTripQuotes();
        testDownwardSegmentStopsOnTick(features);
    }
};

BEAST_DEFINE_TESTSUITE(AMMCurveFunctions, app, xrpl);

}  // namespace xrpl::test
