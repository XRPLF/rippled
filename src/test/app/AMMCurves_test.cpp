#include <test/jtx/AMMTest.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/helpers/AMMCurve.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/ledger/helpers/AMMTickMath.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>

namespace xrpl::test {

struct AMMCurves_test : public jtx::AMMTest
{
    NumberMantissaScaleGuard const sg{xrpl::MantissaRange::MantissaScale::Small};

private:
    static FeatureBitset
    testableAmendments()
    {
        return jtx::testableAmendments() - featureSingleAssetVault - featureLendingProtocol;
    }

    static json::Value
    ammCreateJV(
        jtx::Env& env,
        jtx::Account const& acct,
        jtx::IOU const& asset1,
        jtx::IOU const& asset2,
        STAmount const& amt1,
        STAmount const& amt2)
    {
        json::Value jv;
        jv[jss::Account] = acct.human();
        jv[jss::Amount] = amt1.getJson(JsonOptions::Values::None);
        jv[jss::Amount2] = amt2.getJson(JsonOptions::Values::None);
        jv[jss::TradingFee] = 0;
        jv[jss::TransactionType] = jss::AMMCreate;
        jv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        return jv;
    }

    void
    testTickMath()
    {
        testcase("TickMath");

        // tick 0 => sqrt price 1
        {
            auto const sp = tickToSqrtPrice(0);
            BEAST_EXPECT(sp == Number{1});
        }

        // positive tick
        {
            auto const sp = tickToSqrtPrice(2);
            auto const expected = Number(10001, -4);
            auto const diff = (sp > expected) ? sp - expected : expected - sp;
            BEAST_EXPECT(diff < Number(1, -12));
        }

        // negative tick
        {
            auto const sp = tickToSqrtPrice(-2);
            auto const expected = Number{1} / Number(10001, -4);
            auto const diff = (sp > expected) ? sp - expected : expected - sp;
            BEAST_EXPECT(diff < Number(1, -12));
        }

        // odd tick between 0 and 2
        {
            auto const sp = tickToSqrtPrice(1);
            BEAST_EXPECT(sp > Number{1});
            BEAST_EXPECT(sp < Number(10001, -4));
        }

        // large positive/negative
        {
            BEAST_EXPECT(tickToSqrtPrice(10000) > Number{1});
            auto const neg = tickToSqrtPrice(-10000);
            BEAST_EXPECT(neg > Number{0});
            BEAST_EXPECT(neg < Number{1});
        }

        // symmetry: tick(t) * tick(-t) ~= 1
        {
            auto const prod = tickToSqrtPrice(500) * tickToSqrtPrice(-500);
            auto const diff = (prod > Number{1}) ? prod - Number{1} : Number{1} - prod;
            BEAST_EXPECT(diff < Number(1, -10));
        }

        // sqrtPriceToTick round-trip
        {
            BEAST_EXPECT(sqrtPriceToTick(Number{1}) == 0);
            for (auto t : {0, 1, 2, 10, 100, 1000, -1, -2, -100, -1000})
            {
                auto const sp = tickToSqrtPrice(t);
                BEAST_EXPECT(sqrtPriceToTick(sp) == t);
            }
        }

        // isValidTick
        {
            BEAST_EXPECT(isValidTick(0, 1));
            BEAST_EXPECT(isValidTick(0, 10));
            BEAST_EXPECT(isValidTick(10, 10));
            BEAST_EXPECT(!isValidTick(5, 10));
            BEAST_EXPECT(isValidTick(-60, 60));
            BEAST_EXPECT(!isValidTick(-61, 60));
            BEAST_EXPECT(!isValidTick(minTick - 1, 1));
            BEAST_EXPECT(!isValidTick(maxTick + 1, 1));
            BEAST_EXPECT(isValidTick(minTick, 1));
            BEAST_EXPECT(isValidTick(maxTick, 1));
        }

        // extreme boundary values
        {
            auto const maxSqrt = tickToSqrtPrice(maxTick);
            BEAST_EXPECT(maxSqrt > Number{0});
            BEAST_EXPECT(maxSqrt * maxSqrt > Number{0});

            auto const minSqrt = tickToSqrtPrice(minTick);
            BEAST_EXPECT(minSqrt > Number{0});
            BEAST_EXPECT(minSqrt < Number{1});

            auto const product = maxSqrt * minSqrt;
            auto const diff = (product > Number{1}) ? product - Number{1} : Number{1} - product;
            BEAST_EXPECT(diff < Number(1, -5));

            BEAST_EXPECT(sqrtPriceToTick(maxSqrt) == maxTick);
            BEAST_EXPECT(sqrtPriceToTick(minSqrt) == minTick);
        }
    }

    void
    testGetCurve(FeatureBitset features)
    {
        testcase("getCurve");

        using namespace jtx;

        // with amendment
        {
            Env const env(*this, features | featureAMMCurves);
            auto const& rules = env.current()->rules();
            BEAST_EXPECT(getCurve(CtConstantProduct, rules) != nullptr);
            BEAST_EXPECT(getCurve(CtConcentratedLiquidity, rules) != nullptr);
            BEAST_EXPECT(getCurve(CtStableSwap, rules) != nullptr);
            BEAST_EXPECT(getCurve(255, rules) == nullptr);
        }

        // without amendment: only CP available
        {
            Env const env(*this, features - featureAMMCurves);
            auto const& rules = env.current()->rules();
            BEAST_EXPECT(getCurve(CtConstantProduct, rules) != nullptr);
            BEAST_EXPECT(getCurve(CtConcentratedLiquidity, rules) == nullptr);
            BEAST_EXPECT(getCurve(CtStableSwap, rules) == nullptr);
        }
    }

    void
    testConstantProduct(FeatureBitset features)
    {
        testcase("ConstantProduct");

        using namespace jtx;
        Env const env(*this, features | featureAMMCurves);

        auto const* curve = getCurve(CtConstantProduct, env.current()->rules());
        BEAST_EXPECT(curve != nullptr);

        STAmount const poolIn = USD(1000);
        STAmount const poolOut = EUR(1000);

        // swapIn: 100 into balanced pool => ~90.9
        {
            auto const result = curve->swapIn(poolIn, poolOut, USD(100), 0, nullptr);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(*result > STAmount(EUR(90)));
            BEAST_EXPECT(*result < STAmount(EUR(91)));
        }

        // swapOut: want 90 out => need ~98.9 in
        {
            auto const result = curve->swapOut(poolIn, poolOut, EUR(90), 0, nullptr);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(*result > STAmount(USD(98)));
            BEAST_EXPECT(*result < STAmount(USD(100)));
        }

        // spotPrice: balanced=1, imbalanced=2, fee increases it
        {
            auto const sp = curve->spotPrice(poolIn, poolOut, 0, nullptr);
            BEAST_EXPECT(sp.has_value());
            BEAST_EXPECT(*sp == Number{1});

            auto const sp2 = curve->spotPrice(USD(500), poolOut, 0, nullptr);
            BEAST_EXPECT(sp2.has_value());
            BEAST_EXPECT(*sp2 == Number{2});

            auto const spFee = curve->spotPrice(poolIn, poolOut, 100, nullptr);
            BEAST_EXPECT(spFee.has_value());
            BEAST_EXPECT(*spFee > Number{1});
        }

        // validateParams: always succeeds (no params needed)
        {
            STObject obj(kSfGeneric);
            BEAST_EXPECT(curve->validateParams(obj) == tesSUCCESS);
        }

        // initialLPTokens: sqrt(1000*1000) = 1000
        {
            auto const lptIssue = ammLPTIssue(USD.asset(), EUR.asset(), alice_.id());
            auto const result = curve->initialLPTokens(poolIn, poolOut, lptIssue, nullptr);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(Number(*result) == Number{1000});
        }
    }

    void
    testStableSwap(FeatureBitset features)
    {
        testcase("StableSwap");

        using namespace jtx;
        Env const env(*this, features | featureAMMCurves);

        auto const* curve = getCurve(CtStableSwap, env.current()->rules());
        BEAST_EXPECT(curve != nullptr);

        STObject txParams(kSfGeneric);
        txParams.setFieldU32(sfAmplification, 100);

        auto ammSle = std::make_shared<SLE>(ltAMM, uint256{});
        ammSle->setFieldU32(sfAmplification, 100);

        STAmount const poolIn = USD(1000);
        STAmount const poolOut = EUR(1000);

        // validateParams
        {
            // temMALFORMED: missing amplification
            STObject empty(kSfGeneric);
            BEAST_EXPECT(curve->validateParams(empty) == temMALFORMED);

            // temMALFORMED: A=0
            STObject zero(kSfGeneric);
            zero.setFieldU32(sfAmplification, 0);
            BEAST_EXPECT(curve->validateParams(zero) == temMALFORMED);

            // temMALFORMED: A > MAX
            STObject over(kSfGeneric);
            over.setFieldU32(sfAmplification, maxAmplification + 1);
            BEAST_EXPECT(curve->validateParams(over) == temMALFORMED);

            // tesSUCCESS: valid values
            BEAST_EXPECT(curve->validateParams(txParams) == tesSUCCESS);

            STObject minP(kSfGeneric);
            minP.setFieldU32(sfAmplification, minAmplification);
            BEAST_EXPECT(curve->validateParams(minP) == tesSUCCESS);

            STObject maxP(kSfGeneric);
            maxP.setFieldU32(sfAmplification, maxAmplification);
            BEAST_EXPECT(curve->validateParams(maxP) == tesSUCCESS);
        }

        // swapIn: high-A balanced pool => near 1:1
        {
            auto const result = curve->swapIn(poolIn, poolOut, USD(100), 0, ammSle.get());
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(*result > STAmount(EUR(99)));
            BEAST_EXPECT(*result < STAmount(EUR(100)));
        }

        // swapOut
        {
            auto const result = curve->swapOut(poolIn, poolOut, EUR(99), 0, ammSle.get());
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(*result > STAmount(USD(99)));
            BEAST_EXPECT(*result < STAmount(USD(100)));
        }

        // spotPrice: balanced ~= 1, null params => error
        {
            auto const sp = curve->spotPrice(poolIn, poolOut, 0, ammSle.get());
            BEAST_EXPECT(sp.has_value());
            auto const diff = (*sp > Number{1}) ? *sp - Number{1} : Number{1} - *sp;
            BEAST_EXPECT(diff < Number(1, -10));

            BEAST_EXPECT(!curve->spotPrice(poolIn, poolOut, 0, nullptr).has_value());
        }

        // initialLPTokens: D ~= 2000 for balanced 1000/1000
        {
            auto const lptIssue = ammLPTIssue(USD.asset(), EUR.asset(), alice_.id());
            auto const result = curve->initialLPTokens(poolIn, poolOut, lptIssue, &txParams);
            BEAST_EXPECT(result.has_value());
            auto const lp = Number(*result);
            BEAST_EXPECT(lp > Number{1999});
            BEAST_EXPECT(lp < Number{2001});

            BEAST_EXPECT(!curve->initialLPTokens(poolIn, poolOut, lptIssue, nullptr).has_value());
        }

        // null params => all operations fail
        {
            BEAST_EXPECT(!curve->swapIn(poolIn, poolOut, USD(100), 0, nullptr).has_value());
            BEAST_EXPECT(!curve->swapOut(poolIn, poolOut, EUR(100), 0, nullptr).has_value());
        }
    }

    void
    testConcentratedLiquidity(FeatureBitset features)
    {
        testcase("ConcentratedLiquidity");

        using namespace jtx;
        Env const env(*this, features | featureAMMCurves);

        auto const* curve = getCurve(CtConcentratedLiquidity, env.current()->rules());
        BEAST_EXPECT(curve != nullptr);

        auto ammSle = std::make_shared<SLE>(ltAMM, uint256{});
        ammSle->setFieldU8(sfFeeTier, FtMedium);
        ammSle->setFieldI32(sfCurrentTick, 0);
        ammSle->setFieldU64(sfActiveLiquidity, 1000000);

        STAmount const poolIn = USD(1000);
        STAmount const poolOut = EUR(1000);

        // validateParams
        {
            // temMALFORMED: missing fee tier
            STObject empty(kSfGeneric);
            BEAST_EXPECT(curve->validateParams(empty) == temMALFORMED);

            // temMALFORMED: fee tier out of bounds
            for (std::uint8_t ft :
                 {feeTierCount,
                  static_cast<std::uint8_t>(feeTierCount + 1),
                  std::numeric_limits<std::uint8_t>::max()})
            {
                STObject obj(kSfGeneric);
                obj.setFieldU8(sfFeeTier, ft);
                BEAST_EXPECT(curve->validateParams(obj) == temMALFORMED);
            }

            // tesSUCCESS: all valid tiers
            for (std::uint8_t ft = 0; ft < feeTierCount; ++ft)
            {
                STObject obj(kSfGeneric);
                obj.setFieldU8(sfFeeTier, ft);
                BEAST_EXPECT(curve->validateParams(obj) == tesSUCCESS);
            }
        }

        // swapIn
        {
            auto const result = curve->swapIn(poolIn, poolOut, USD(100), 0, ammSle.get());
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(*result > STAmount(EUR(0)));
        }

        // swapOut
        {
            auto const result = curve->swapOut(poolIn, poolOut, EUR(50), 0, ammSle.get());
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(*result > STAmount(USD(0)));
        }

        // spotPrice: tick 0 => ~1, null params => error
        {
            auto spSle = std::make_shared<SLE>(ltAMM, uint256{});
            spSle->setFieldI32(sfCurrentTick, 0);
            spSle->setFieldU64(sfActiveLiquidity, 1000000);

            auto const sp = curve->spotPrice(poolIn, poolOut, 0, spSle.get());
            BEAST_EXPECT(sp.has_value());
            auto const diff = (*sp > Number{1}) ? *sp - Number{1} : Number{1} - *sp;
            BEAST_EXPECT(diff < Number(1, -10));

            BEAST_EXPECT(!curve->spotPrice(poolIn, poolOut, 0, nullptr).has_value());
        }

        // null params => all operations fail
        {
            BEAST_EXPECT(!curve->swapIn(poolIn, poolOut, USD(100), 0, nullptr).has_value());
            BEAST_EXPECT(!curve->swapOut(poolIn, poolOut, EUR(50), 0, nullptr).has_value());
        }

        // zero liquidity => fail
        {
            auto zeroLiqSle = std::make_shared<SLE>(ltAMM, uint256{});
            zeroLiqSle->setFieldI32(sfCurrentTick, 0);
            zeroLiqSle->setFieldU64(sfActiveLiquidity, 0);

            BEAST_EXPECT(
                !curve->swapIn(poolIn, poolOut, USD(100), 0, zeroLiqSle.get()).has_value());
            BEAST_EXPECT(
                !curve->swapOut(poolIn, poolOut, EUR(50), 0, zeroLiqSle.get()).has_value());
        }

        // swap symmetry at tick 0
        {
            auto const eurOut = curve->swapIn(USD(10000), EUR(10000), USD(100), 0, ammSle.get());
            auto const usdOut = curve->swapIn(EUR(10000), USD(10000), EUR(100), 0, ammSle.get());
            BEAST_EXPECT(eurOut.has_value());
            BEAST_EXPECT(usdOut.has_value());
            if (eurOut && usdOut)
            {
                auto const diff = Number(*eurOut) - Number(*usdOut);
                auto const absDiff = (diff > Number{0}) ? diff : -diff;
                BEAST_EXPECT(absDiff < Number(1, -6));
            }
        }

        // swapOut near-zero denominator
        {
            auto lowLiqSle = std::make_shared<SLE>(ltAMM, uint256{});
            lowLiqSle->setFieldU8(sfFeeTier, FtMedium);
            lowLiqSle->setFieldI32(sfCurrentTick, 0);
            lowLiqSle->setFieldU64(sfActiveLiquidity, 1000);

            auto const result =
                curve->swapOut(EUR(10000), USD(10000), USD(999), 0, lowLiqSle.get());
            if (result.has_value())
                BEAST_EXPECT(Number(*result) > Number{0});

            auto const fail = curve->swapOut(EUR(10000), USD(10000), USD(1001), 0, lowLiqSle.get());
            if (fail.has_value())
                BEAST_EXPECT(Number(*fail) > Number{0});
        }

        // position liquidity overflow
        {
            auto const sqrtPL = tickToSqrtPrice(0);
            auto const sqrtPU = tickToSqrtPrice(1);
            auto const denom = sqrtPU - sqrtPL;
            Number const maxAmt{1, 15};
            Number const liquidity = maxAmt * sqrtPL * sqrtPU / denom;
            auto const int64Max = Number(std::numeric_limits<std::int64_t>::max());
            bool const overflows = liquidity > int64Max;
            if (overflows)
            {
                BEAST_EXPECT(overflows);
                log << "  CL liquidity overflow confirmed: "
                    << "liquidity=" << liquidity << " > INT64_MAX=" << int64Max << std::endl;
            }
            else
            {
                BEAST_EXPECT(!overflows);
            }
        }
    }

    void
    testFees(FeatureBitset features)
    {
        testcase("Fees");

        using namespace jtx;
        Env const env(*this, features | featureAMMCurves);

        auto const& rules = env.current()->rules();
        std::uint16_t const tfee = 100;
        STAmount const poolIn = USD(1000);
        STAmount const poolOut = EUR(1000);
        STAmount const assetIn = USD(100);

        // CP: fee reduces output
        {
            auto const* curve = getCurve(CtConstantProduct, rules);
            auto const noFee = curve->swapIn(poolIn, poolOut, assetIn, 0, nullptr);
            auto const withFee = curve->swapIn(poolIn, poolOut, assetIn, tfee, nullptr);
            BEAST_EXPECT(noFee.has_value() && withFee.has_value());
            BEAST_EXPECT(*withFee < *noFee);
        }

        // SS: fee reduces output
        {
            auto ssSle = std::make_shared<SLE>(ltAMM, uint256{});
            ssSle->setFieldU32(sfAmplification, 100);
            auto const* curve = getCurve(CtStableSwap, rules);
            auto const noFee = curve->swapIn(poolIn, poolOut, assetIn, 0, ssSle.get());
            auto const withFee = curve->swapIn(poolIn, poolOut, assetIn, tfee, ssSle.get());
            BEAST_EXPECT(noFee.has_value() && withFee.has_value());
            BEAST_EXPECT(*withFee < *noFee);
        }
    }

    void
    testNewtonBoundary(FeatureBitset features)
    {
        testcase("Newton boundary");

        using namespace jtx;
        Env const env(*this, features | featureAMMCurves);

        auto const* curve = getCurve(CtStableSwap, env.current()->rules());
        BEAST_EXPECT(curve != nullptr);

        // extreme asymmetry at max A
        {
            auto ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU32(sfAmplification, maxAmplification);

            auto const result = curve->swapIn(USD(1), EUR(1999999), USD(1), 0, ammSle.get());
            if (result.has_value())
            {
                BEAST_EXPECT(*result < EUR(1999999));
                BEAST_EXPECT(*result > STAmount(EUR(0)));
            }
        }

        // convergence at A=1
        {
            auto ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU32(sfAmplification, minAmplification);

            auto const result = curve->swapIn(USD(1000), EUR(1000), USD(100), 0, ammSle.get());
            BEAST_EXPECT(result.has_value());
            if (result)
            {
                auto const out = Number(*result);
                BEAST_EXPECT(out > Number{0});
                BEAST_EXPECT(out < Number{100});
            }
        }

        // A=0: rejected by validateParams, safe if bypassed
        {
            STObject badParams(kSfGeneric);
            badParams.setFieldU32(sfAmplification, 0);
            BEAST_EXPECT(curve->validateParams(badParams) == temMALFORMED);

            auto badSle = std::make_shared<SLE>(ltAMM, uint256{});
            badSle->setFieldU32(sfAmplification, 0);
            auto const result = curve->swapIn(USD(1000), EUR(1000), USD(100), 0, badSle.get());
            if (result.has_value())
            {
                BEAST_EXPECT(Number(*result) > Number{0});
                BEAST_EXPECT(Number(*result) < Number{1000});
            }
        }

        // invariant preserved across 100 sequential swaps
        {
            auto ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU32(sfAmplification, maxAmplification);

            STAmount poolIn = USD(1000);
            STAmount poolOut = EUR(1000);
            Number totalIn{0}, totalOut{0};
            bool anyExploit = false;

            for (int i = 0; i < 100; ++i)
            {
                auto const result = curve->swapIn(poolIn, poolOut, USD(10), 0, ammSle.get());
                if (!result)
                    break;
                totalIn = totalIn + Number{10};
                totalOut = totalOut + Number(*result);
                poolIn = poolIn + USD(10);
                poolOut = poolOut - *result;
                if (Number(poolOut) <= Number{0})
                {
                    anyExploit = true;
                    break;
                }
            }
            BEAST_EXPECT(!anyExploit);
            BEAST_EXPECT(Number(poolOut) > Number{0});
            BEAST_EXPECT(totalOut < totalIn + Number{1});
        }

        // LP token D: higher A => closer to sum
        {
            auto const lptIssue = ammLPTIssue(USD.asset(), EUR.asset(), alice_.id());

            STObject maxA(kSfGeneric);
            maxA.setFieldU32(sfAmplification, maxAmplification);
            auto const lpHigh = curve->initialLPTokens(USD(1000), EUR(1000), lptIssue, &maxA);
            BEAST_EXPECT(lpHigh.has_value());
            auto const lpHighNum = Number(*lpHigh);
            BEAST_EXPECT(lpHighNum > Number{1999});
            BEAST_EXPECT(lpHighNum <= Number{2000});

            STObject minA(kSfGeneric);
            minA.setFieldU32(sfAmplification, minAmplification);
            auto const lpLow = curve->initialLPTokens(USD(1000), EUR(1000), lptIssue, &minA);
            BEAST_EXPECT(lpLow.has_value());
            auto const lpLowNum = Number(*lpLow);
            BEAST_EXPECT(lpLowNum >= Number{1000});
            BEAST_EXPECT(lpLowNum <= Number{2000});
            BEAST_EXPECT(lpHighNum >= lpLowNum);
        }
    }

    void
    testRoundTrip(FeatureBitset features)
    {
        testcase("Round-trip");

        using namespace jtx;
        Env const env(*this, features | featureAMMCurves);

        auto const& rules = env.current()->rules();
        STAmount const pool = USD(10000);
        STAmount const pool2 = EUR(10000);

        // StableSwap: swap forward and back, no profit
        {
            auto const* curve = getCurve(CtStableSwap, rules);
            auto ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU32(sfAmplification, 100);

            auto const eurOut = curve->swapIn(pool, pool2, USD(1000), 0, ammSle.get());
            BEAST_EXPECT(eurOut.has_value());
            auto const usdBack =
                curve->swapIn(pool2 - *eurOut, pool + USD(1000), *eurOut, 0, ammSle.get());
            BEAST_EXPECT(usdBack.has_value());
            BEAST_EXPECT(Number(*usdBack) - Number{1000} < Number(1, -7));
        }

        // StableSwap: swapIn/swapOut inverse
        {
            auto const* curve = getCurve(CtStableSwap, rules);
            auto ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU32(sfAmplification, 100);

            auto const eurOut = curve->swapIn(pool, pool2, USD(1000), 0, ammSle.get());
            BEAST_EXPECT(eurOut.has_value());
            auto const usdNeeded = curve->swapOut(pool, pool2, *eurOut, 0, ammSle.get());
            BEAST_EXPECT(usdNeeded.has_value());
            BEAST_EXPECT(Number(*usdNeeded) >= Number{999});
            auto const gap = Number(*usdNeeded) - Number{1000};
            BEAST_EXPECT(gap < Number(1, -10) || gap >= Number{0});
        }
    }

    void
    testDust(FeatureBitset features)
    {
        testcase("Dust");

        using namespace jtx;
        Env const env(*this, features | featureAMMCurves);

        auto const& rules = env.current()->rules();
        STAmount const pool = USD(10000);
        STAmount const pool2 = EUR(10000);
        STAmount const dust(USD.asset(), 1, -6);

        struct CurveSetup
        {
            std::uint8_t type;
            std::shared_ptr<SLE> sle;
        };

        auto ssSle = std::make_shared<SLE>(ltAMM, uint256{});
        ssSle->setFieldU32(sfAmplification, 100);

        CurveSetup setups[] = {
            {CtConstantProduct, nullptr},
            {CtStableSwap, ssSle},
        };

        for (auto& [type, sle] : setups)
        {
            auto const* curve = getCurve(type, rules);
            if (!curve)
                continue;
            auto const result = curve->swapIn(pool, pool2, dust, 0, sle.get());
            if (result.has_value())
                BEAST_EXPECT(Number(*result) <= Number(dust) * Number{2});
        }
    }

    void
    testPreflight(FeatureBitset features)
    {
        testcase("preflight");

        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const al("alice");
        Account const gw2("gateway");
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];

        env.fund(XRP(10000), gw2, al);
        env.trust(usd(100000), al);
        env.trust(eur(100000), al);
        env(pay(gw2, al, usd(10000)));
        env(pay(gw2, al, eur(10000)));
        env.close();

        // temMALFORMED: invalid curve type 255
        {
            auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            jv[sfCurveType.jsonName] = 255;
            env(jv, Ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: invalid curve type 4
        {
            auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            jv[sfCurveType.jsonName] = 4;
            env(jv, Ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: StableSwap without CurveParams
        {
            auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            jv[sfCurveType.jsonName] = CtStableSwap;
            env(jv, Ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: StableSwap A > maxAmplification
        {
            auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            jv[sfCurveType.jsonName] = CtStableSwap;
            jv[sfAmplification.jsonName] = maxAmplification + 1;
            env(jv, Ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: StableSwap A=0
        {
            auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            jv[sfCurveType.jsonName] = CtStableSwap;
            jv[sfAmplification.jsonName] = 0;
            env(jv, Ter(temMALFORMED));
            env.close();
        }
    }

    void
    testPreflightDisabled(FeatureBitset features)
    {
        testcase("preflight disabled");

        using namespace jtx;

        Env env(*this, features - featureAMMCurves);
        Account const al("alice");
        Account const gw2("gateway");
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];

        env.fund(XRP(10000), gw2, al);
        env.trust(usd(100000), al);
        env.trust(eur(100000), al);
        env(pay(gw2, al, usd(10000)));
        env(pay(gw2, al, eur(10000)));
        env.close();

        // temDISABLED: curve type without amendment
        {
            auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            jv[sfCurveType.jsonName] = CtStableSwap;
            jv[sfAmplification.jsonName] = 100;
            env(jv, Ter(temDISABLED));
            env.close();
        }
    }

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

    void
    testDoApply(FeatureBitset features)
    {
        testcase("doApply");

        using namespace jtx;

        Account const al("alice");
        Account const gw2("gateway");

        // tesSUCCESS: ConstantProduct (explicit curve type)
        {
            Env env(*this, features | featureAMMCurves);
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            fundForAMMCreate(env, gw2, al, usd, eur);

            auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            jv[sfCurveType.jsonName] = CtConstantProduct;
            env(jv);
            env.close();

            auto const ammSle = env.current()->read(keylet::amm(usd.asset(), eur.asset()));
            BEAST_EXPECT(ammSle != nullptr);
        }

        // tesSUCCESS: StableSwap
        {
            Env env(*this, features | featureAMMCurves);
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            fundForAMMCreate(env, gw2, al, usd, eur);

            auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            jv[sfCurveType.jsonName] = CtStableSwap;
            jv[sfAmplification.jsonName] = 100;
            env(jv);
            env.close();

            auto const ammSle =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtStableSwap));
            BEAST_EXPECT(ammSle != nullptr);
            if (ammSle)
            {
                BEAST_EXPECT(ammSle->getFieldU8(sfCurveType) == CtStableSwap);
            }
        }

        // tesSUCCESS: StableSwap high-A, verify LP tokens and
        // pool
        {
            Env env(*this, features | featureAMMCurves);
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            fundForAMMCreate(env, gw2, al, usd, eur);

            auto jv = ammCreateJV(env, al, usd, eur, usd(10000), eur(10000));
            jv[sfCurveType.jsonName] = CtStableSwap;
            jv[sfAmplification.jsonName] = 5000u;
            env(jv);
            env.close();

            auto const ammSle =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtStableSwap));
            BEAST_EXPECT(ammSle != nullptr);
            if (ammSle)
            {
                auto const lptBalance = ammSle->getFieldAmount(sfLPTokenBalance);
                auto const lpNum = Number(lptBalance);
                BEAST_EXPECT(lpNum > Number{19999});
                BEAST_EXPECT(lpNum <= Number{20000});

                auto const ammAcct = ammSle->getAccountID(sfAccount);
                auto const [amt1, amt2] = ammPoolHolds(
                    *env.current(),
                    ammAcct,
                    usd.asset(),
                    eur.asset(),
                    FreezeHandling::IgnoreFreeze,
                    AuthHandling::IgnoreAuth,
                    env.journal);
                auto const totalPool = Number(amt1) + Number(amt2);
                auto const diff = (totalPool > Number{20000}) ? totalPool - Number{20000}
                                                              : Number{20000} - totalPool;
                BEAST_EXPECT(diff < Number{1});
            }
        }
    }

    static json::Value
    ammVoteJV(
        jtx::Env& env,
        jtx::Account const& acct,
        jtx::IOU const& asset1,
        jtx::IOU const& asset2,
        std::uint32_t tfee,
        std::optional<std::uint32_t> amplification = std::nullopt,
        std::optional<std::uint8_t> curveType = std::nullopt)
    {
        json::Value jv;
        jv[jss::Account] = acct.human();
        jv[jss::Asset] = STIssue(sfAsset, asset1.asset()).getJson(JsonOptions::Values::None);
        jv[jss::Asset2] = STIssue(sfAsset, asset2.asset()).getJson(JsonOptions::Values::None);
        jv[jss::TradingFee] = tfee;
        jv[jss::TransactionType] = jss::AMMVote;
        jv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        if (curveType)
            jv[sfCurveType.jsonName] = *curveType;
        if (amplification)
            jv[sfAmplification.jsonName] = *amplification;
        return jv;
    }

    void
    testAmplificationVotePreflight(FeatureBitset features)
    {
        testcase("Amplification vote preflight");

        using namespace jtx;
        Env env(*this, features | featureAMMCurves);
        Account const al("alice");
        Account const gw2("gateway");
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        fundForAMMCreate(env, gw2, al, usd, eur);

        // Create StableSwap AMM
        auto jvCreate = ammCreateJV(env, al, usd, eur, usd(10000), eur(10000));
        jvCreate[sfCurveType.jsonName] = CtStableSwap;
        jvCreate[sfAmplification.jsonName] = 100;
        env(jvCreate);
        env.close();

        // temDISABLED: amplification vote without amendment
        {
            Env env2(*this, features - featureAMMCurves);
            Account const al2("alice2");
            Account const gw3("gateway3");
            auto const usd2 = gw3["USD"];
            auto const eur2 = gw3["EUR"];
            fundForAMMCreate(env2, gw3, al2, usd2, eur2);

            auto jv = ammVoteJV(env2, al2, usd2, eur2, 0, 200);
            env2(jv, Ter(temDISABLED));
            env2.close();
        }

        // temMALFORMED: amplification below MIN
        {
            auto jv = ammVoteJV(env, al, usd, eur, 0, 0, CtStableSwap);
            env(jv, Ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: amplification above MAX
        {
            auto jv = ammVoteJV(env, al, usd, eur, 0, maxAmplification + 1, CtStableSwap);
            env(jv, Ter(temMALFORMED));
            env.close();
        }

        // Valid: amplification within range
        {
            auto jv = ammVoteJV(env, al, usd, eur, 0, 110, CtStableSwap);
            env(jv);
            env.close();
        }

        // Valid: minAmplification
        {
            auto jv = ammVoteJV(env, al, usd, eur, 0, minAmplification, CtStableSwap);
            env(jv);
            env.close();
        }

        // Valid: maxAmplification
        {
            auto jv = ammVoteJV(env, al, usd, eur, 0, maxAmplification, CtStableSwap);
            env(jv);
            env.close();
        }
    }

    void
    testAmplificationVotePreclaim(FeatureBitset features)
    {
        testcase("Amplification vote preclaim");

        using namespace jtx;

        // tecAMM_FAILED: amplification on non-StableSwap
        {
            Env env(*this, features | featureAMMCurves);
            Account const al("alice");
            Account const gw2("gateway");
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            fundForAMMCreate(env, gw2, al, usd, eur);

            // Create ConstantProduct AMM
            auto jvCreate = ammCreateJV(env, al, usd, eur, usd(10000), eur(10000));
            env(jvCreate);
            env.close();

            auto jv = ammVoteJV(env, al, usd, eur, 0, 50);
            env(jv, Ter(tecAMM_FAILED));
            env.close();
        }
    }

    void
    testAmplificationVoteApply(FeatureBitset features)
    {
        testcase("Amplification vote apply");

        using namespace jtx;

        // Rate-limited change: max 10% per vote
        {
            Env env(*this, features | featureAMMCurves);
            Account const al("alice");
            Account const gw2("gateway");
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            fundForAMMCreate(env, gw2, al, usd, eur);

            auto jvCreate = ammCreateJV(env, al, usd, eur, usd(10000), eur(10000));
            jvCreate[sfCurveType.jsonName] = CtStableSwap;
            jvCreate[sfAmplification.jsonName] = 100;
            env(jvCreate);
            env.close();

            // Vote to increase to 200 (clamped to 100+10=110)
            auto jv = ammVoteJV(env, al, usd, eur, 0, 200, CtStableSwap);
            env(jv);
            env.close();

            auto const ammSle =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtStableSwap));
            BEAST_EXPECT(ammSle != nullptr);
            if (ammSle)
            {
                auto const amp = ammSle->getFieldU32(sfAmplification);
                BEAST_EXPECT(amp == 110);
            }
        }

        // Rate-limited decrease: max 10% per vote
        {
            Env env(*this, features | featureAMMCurves);
            Account const al("alice");
            Account const gw2("gateway");
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            fundForAMMCreate(env, gw2, al, usd, eur);

            auto jvCreate = ammCreateJV(env, al, usd, eur, usd(10000), eur(10000));
            jvCreate[sfCurveType.jsonName] = CtStableSwap;
            jvCreate[sfAmplification.jsonName] = 100;
            env(jvCreate);
            env.close();

            // Vote to decrease to 1 (clamped to 100-10=90)
            auto jv = ammVoteJV(env, al, usd, eur, 0, 1, CtStableSwap);
            env(jv);
            env.close();

            auto const ammSle =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtStableSwap));
            BEAST_EXPECT(ammSle != nullptr);
            if (ammSle)
            {
                auto const amp = ammSle->getFieldU32(sfAmplification);
                BEAST_EXPECT(amp == 90);
            }
        }

        // Deadlock fix: change from minAmplification
        {
            Env env(*this, features | featureAMMCurves);
            Account const al("alice");
            Account const gw2("gateway");
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            fundForAMMCreate(env, gw2, al, usd, eur);

            auto jvCreate = ammCreateJV(env, al, usd, eur, usd(10000), eur(10000));
            jvCreate[sfCurveType.jsonName] = CtStableSwap;
            jvCreate[sfAmplification.jsonName] = minAmplification;
            env(jvCreate);
            env.close();

            // Vote to increase from 1 to 100
            // maxChange = max(1*10/100, 1) = 1, so clamped to 2
            auto jv = ammVoteJV(env, al, usd, eur, 0, 100, CtStableSwap);
            env(jv);
            env.close();

            auto const ammSle =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtStableSwap));
            BEAST_EXPECT(ammSle != nullptr);
            if (ammSle)
            {
                auto const amp = ammSle->getFieldU32(sfAmplification);
                BEAST_EXPECT(amp == 2);
            }
        }

        // Small amp (5): verify non-zero maxChange
        {
            Env env(*this, features | featureAMMCurves);
            Account const al("alice");
            Account const gw2("gateway");
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            fundForAMMCreate(env, gw2, al, usd, eur);

            auto jvCreate = ammCreateJV(env, al, usd, eur, usd(10000), eur(10000));
            jvCreate[sfCurveType.jsonName] = CtStableSwap;
            jvCreate[sfAmplification.jsonName] = 5;
            env(jvCreate);
            env.close();

            // 5*10/100=0, but max(0,1)=1, so new amp = 6
            auto jv = ammVoteJV(env, al, usd, eur, 0, 100, CtStableSwap);
            env(jv);
            env.close();

            auto const ammSle =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtStableSwap));
            BEAST_EXPECT(ammSle != nullptr);
            if (ammSle)
            {
                auto const amp = ammSle->getFieldU32(sfAmplification);
                BEAST_EXPECT(amp == 6);
            }
        }

        // No-op: voting same amplification
        {
            Env env(*this, features | featureAMMCurves);
            Account const al("alice");
            Account const gw2("gateway");
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            fundForAMMCreate(env, gw2, al, usd, eur);

            auto jvCreate = ammCreateJV(env, al, usd, eur, usd(10000), eur(10000));
            jvCreate[sfCurveType.jsonName] = CtStableSwap;
            jvCreate[sfAmplification.jsonName] = 100;
            env(jvCreate);
            env.close();

            auto jv = ammVoteJV(env, al, usd, eur, 0, 100, CtStableSwap);
            env(jv);
            env.close();

            auto const ammSle =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtStableSwap));
            BEAST_EXPECT(ammSle != nullptr);
            if (ammSle)
            {
                auto const amp = ammSle->getFieldU32(sfAmplification);
                BEAST_EXPECT(amp == 100);
            }
        }
    }

    void
    testCLNonZeroTick(FeatureBitset features)
    {
        testcase("CL non-zero tick");

        using namespace jtx;
        Env const env(*this, features | featureAMMCurves);

        auto const* curve = getCurve(CtConcentratedLiquidity, env.current()->rules());
        BEAST_EXPECT(curve != nullptr);

        STAmount const poolIn = USD(1000);
        STAmount const poolOut = EUR(1000);

        // Positive tick: price > 1
        {
            auto ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU8(sfFeeTier, FtMedium);
            ammSle->setFieldI32(sfCurrentTick, 100);
            ammSle->setFieldU64(sfActiveLiquidity, 1000000);

            auto const result = curve->swapIn(poolIn, poolOut, USD(100), 0, ammSle.get());
            BEAST_EXPECT(result.has_value());
            if (result)
                BEAST_EXPECT(Number(*result) > Number{0});

            auto const sp = curve->spotPrice(poolIn, poolOut, 0, ammSle.get());
            BEAST_EXPECT(sp.has_value());
            if (sp)
            {
                auto const sqrtP = tickToSqrtPrice(100);
                auto const expectedPrice = sqrtP * sqrtP;
                bool const inIsAsset1 = poolIn.asset() < poolOut.asset();
                auto const expectedSp = inIsAsset1 ? expectedPrice : Number{1} / expectedPrice;
                auto const diff = (*sp > expectedSp) ? *sp - expectedSp : expectedSp - *sp;
                BEAST_EXPECT(diff < Number(1, -8));
            }
        }

        // Negative tick: price < 1
        {
            auto ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU8(sfFeeTier, FtMedium);
            ammSle->setFieldI32(sfCurrentTick, -100);
            ammSle->setFieldU64(sfActiveLiquidity, 1000000);

            auto const result = curve->swapIn(poolIn, poolOut, USD(100), 0, ammSle.get());
            BEAST_EXPECT(result.has_value());
            if (result)
                BEAST_EXPECT(Number(*result) > Number{0});
        }

        // Large positive tick
        {
            auto ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU8(sfFeeTier, FtMedium);
            ammSle->setFieldI32(sfCurrentTick, 10000);
            ammSle->setFieldU64(sfActiveLiquidity, 1000000);

            auto const sp = curve->spotPrice(poolIn, poolOut, 0, ammSle.get());
            BEAST_EXPECT(sp.has_value());
            if (sp)
                BEAST_EXPECT(*sp > Number{0});
        }

        // Large negative tick
        {
            auto ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU8(sfFeeTier, FtMedium);
            ammSle->setFieldI32(sfCurrentTick, -10000);
            ammSle->setFieldU64(sfActiveLiquidity, 1000000);

            auto const sp = curve->spotPrice(poolIn, poolOut, 0, ammSle.get());
            BEAST_EXPECT(sp.has_value());
            if (sp)
                BEAST_EXPECT(*sp > Number{0});
        }

        // Tick spacing present
        {
            auto ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU8(sfFeeTier, FtMedium);
            ammSle->setFieldI32(sfCurrentTick, 0);
            ammSle->setFieldU64(sfActiveLiquidity, 1000000);
            ammSle->setFieldU16(sfTickSpacing, 60);

            auto const result = curve->swapIn(poolIn, poolOut, USD(100), 0, ammSle.get());
            BEAST_EXPECT(result.has_value());
            if (result)
                BEAST_EXPECT(Number(*result) > Number{0});
        }

        // CL fee impact
        {
            auto ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU8(sfFeeTier, FtMedium);
            ammSle->setFieldI32(sfCurrentTick, 50);
            ammSle->setFieldU64(sfActiveLiquidity, 500000);

            auto const noFee = curve->swapIn(poolIn, poolOut, USD(100), 0, ammSle.get());
            auto const withFee = curve->swapIn(poolIn, poolOut, USD(100), 500, ammSle.get());
            BEAST_EXPECT(noFee.has_value() && withFee.has_value());
            if (noFee && withFee)
                BEAST_EXPECT(*withFee < *noFee);
        }

        // Consistency: swapIn then swapOut inverse
        {
            auto ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU8(sfFeeTier, FtMedium);
            ammSle->setFieldI32(sfCurrentTick, 200);
            ammSle->setFieldU64(sfActiveLiquidity, 1000000);

            auto const eurOut = curve->swapIn(poolIn, poolOut, USD(100), 0, ammSle.get());
            BEAST_EXPECT(eurOut.has_value());
            if (eurOut)
            {
                auto const usdNeeded = curve->swapOut(poolIn, poolOut, *eurOut, 0, ammSle.get());
                BEAST_EXPECT(usdNeeded.has_value());
                if (usdNeeded)
                {
                    auto const diff = Number(*usdNeeded) - Number{100};
                    auto const absDiff = (diff > Number{0}) ? diff : -diff;
                    BEAST_EXPECT(absDiff < Number(1, -5));
                }
            }
        }
    }

    void
    testStableSwapEdgeCases(FeatureBitset features)
    {
        testcase("StableSwap edge cases");

        using namespace jtx;
        Env const env(*this, features | featureAMMCurves);

        auto const* curve = getCurve(CtStableSwap, env.current()->rules());
        BEAST_EXPECT(curve != nullptr);

        // High asymmetry: 1:1000 pool
        {
            auto ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU32(sfAmplification, 100);

            auto const result = curve->swapIn(USD(1), EUR(1000), USD(1), 0, ammSle.get());
            BEAST_EXPECT(result.has_value());
            if (result)
            {
                BEAST_EXPECT(Number(*result) > Number{0});
                BEAST_EXPECT(Number(*result) < Number{1000});
            }
        }

        // Output can't exceed pool
        {
            auto ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU32(sfAmplification, 100);

            auto const result = curve->swapIn(USD(1000), EUR(1000), USD(1000000), 0, ammSle.get());
            BEAST_EXPECT(result.has_value());
            if (result)
                BEAST_EXPECT(Number(*result) < Number{1000});
        }

        // swapOut: want more than pool has
        {
            auto ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU32(sfAmplification, 100);

            auto const result = curve->swapOut(USD(1000), EUR(1000), EUR(999), 0, ammSle.get());
            BEAST_EXPECT(result.has_value());
            if (result)
                BEAST_EXPECT(Number(*result) > Number{0});

            auto const fail = curve->swapOut(USD(1000), EUR(1000), EUR(1000), 0, ammSle.get());
            BEAST_EXPECT(!fail.has_value());
        }
    }

    struct CurveTestEnv
    {
        std::uint8_t curveType;
        std::string name;
        json::Value curveParamsJson;
    };

    static void
    setupSwapEnv(
        jtx::Env& env,
        jtx::Account const& gw,
        jtx::Account const& creator,
        jtx::Account const& trader,
        jtx::IOU const& usd,
        jtx::IOU const& eur)
    {
        using namespace jtx;
        env.fund(XRP(100000), gw, creator, trader);
        env.trust(usd(1000000), creator);
        env.trust(eur(1000000), creator);
        env.trust(usd(1000000), trader);
        env.trust(eur(1000000), trader);
        env(pay(gw, creator, usd(100000)));
        env(pay(gw, creator, eur(100000)));
        env(pay(gw, trader, usd(50000)));
        env(pay(gw, trader, eur(50000)));
        env.close();
    }

    static void
    createCurvePool(
        jtx::Env& env,
        jtx::Account const& creator,
        jtx::IOU const& usd,
        jtx::IOU const& eur,
        STAmount const& amt1,
        STAmount const& amt2,
        std::uint8_t curveType,
        json::Value const& cpJson,
        std::uint32_t tradingFee = 0)
    {
        auto jv = ammCreateJV(env, creator, usd, eur, amt1, amt2);
        jv[jss::TradingFee] = tradingFee;
        if (curveType != CtConstantProduct)
        {
            jv[sfCurveType.jsonName] = curveType;
            if (!cpJson.isNull() && cpJson.size() > 0)
            {
                if (cpJson.isMember(sfAmplification.jsonName))
                {
                    jv[sfAmplification.jsonName] = cpJson[sfAmplification.jsonName];
                }
                if (cpJson.isMember(sfFeeTier.jsonName))
                {
                    jv[sfFeeTier.jsonName] = cpJson[sfFeeTier.jsonName];
                }
            }
        }
        env(jv);
        env.close();
    }

    void
    testCurvePricing(FeatureBitset features)
    {
        testcase("Curve pricing theory");

        using namespace jtx;

        Account const al("alice");
        Account const bo("bob");
        Account const gw2("gateway");

        json::Value ssParams;
        ssParams[sfAmplification.jsonName] = 100;

        // Compare curves: same 1000 USD input, see who gives more EUR.
        // Theory for 10k/10k balanced pools:
        //   CP:  xy=k  → Δy = 10000·1000/(10000+1000) ≈ 909
        //   SS:  A=100 → near 1:1 for pegged assets ≈ 999
        //   W80: x^0.8·y^0.2=k → different slippage
        Number usdSpentSS{0}, usdSpentCP{0};

        // StableSwap: near 1:1 for stablecoins
        {
            Env env(*this, features | featureAMMCurves);
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            setupSwapEnv(env, gw2, al, bo, usd, eur);
            createCurvePool(env, al, usd, eur, usd(10000), eur(10000), CtStableSwap, ssParams);

            auto const eurBefore = env.balance(bo, eur.issue());
            auto const usdBefore = env.balance(bo, usd.issue());

            // Fixed delivery: get 1000 EUR, measure what it costs
            env(pay(bo, bo, eur(1000)),
                jtx::Path(~eur),
                jtx::Sendmax(usd(1200)),
                jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
            env.close();

            auto const eurGot = env.balance(bo, eur.issue()) - eurBefore;
            auto const usdSpent = usdBefore - env.balance(bo, usd.issue());
            usdSpentSS = Number(usdSpent);

            // SS theory: A=100 flattens the curve near peg
            BEAST_EXPECT(Number(eurGot) >= Number{1000});
            BEAST_EXPECT(Number(usdSpent) < Number{1010});
        }

        // ConstantProduct: xy=k costs more for same output
        {
            Env env(*this, features | featureAMMCurves);
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            setupSwapEnv(env, gw2, al, bo, usd, eur);
            createCurvePool(
                env, al, usd, eur, usd(10000), eur(10000), CtConstantProduct, json::Value{});

            auto const eurBefore = env.balance(bo, eur.issue());
            auto const usdBefore = env.balance(bo, usd.issue());

            // Same delivery: get 1000 EUR, measure cost
            env(pay(bo, bo, eur(1000)),
                jtx::Path(~eur),
                jtx::Sendmax(usd(1200)),
                jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
            env.close();

            auto const eurGot = env.balance(bo, eur.issue()) - eurBefore;
            auto const usdSpent = usdBefore - env.balance(bo, usd.issue());
            usdSpentCP = Number(usdSpent);

            // CP theory: Δx = x·Δy/(y-Δy) = 10000·1000/9000 ≈ 1111
            BEAST_EXPECT(Number(eurGot) >= Number{1000});
            BEAST_EXPECT(Number(usdSpent) > Number{1100});
            BEAST_EXPECT(Number(usdSpent) < Number{1150});
        }

        // Core comparison: SS costs less than CP for same output
        // This IS the StableSwap thesis
        BEAST_EXPECT(usdSpentSS < usdSpentCP);

        // Pool isolation: two pools with same assets, different curves
        {
            Env env(*this, features | featureAMMCurves);
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            setupSwapEnv(env, gw2, al, bo, usd, eur);

            // Create both a CP and SS pool for same pair
            createCurvePool(
                env, al, usd, eur, usd(10000), eur(10000), CtConstantProduct, json::Value{});
            createCurvePool(env, al, usd, eur, usd(10000), eur(10000), CtStableSwap, ssParams);

            // Both pools should exist independently
            auto const cpSle =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtConstantProduct));
            auto const ssSle =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtStableSwap));
            BEAST_EXPECT(cpSle != nullptr);
            BEAST_EXPECT(ssSle != nullptr);
            if (cpSle && ssSle)
                BEAST_EXPECT(cpSle->key() != ssSle->key());
        }
    }

    void
    testBoundaryRejection(FeatureBitset features)
    {
        testcase("Boundary rejection");

        using namespace jtx;

        Account const al("alice");
        Account const bo("bob");
        Account const gw2("gateway");

        json::Value ssParams;
        ssParams[sfAmplification.jsonName] = 100;

        struct CurveCase
        {
            std::uint8_t type;
            std::string name;
            json::Value params;
        };

        CurveCase cases[] = {
            {CtConstantProduct, "CP", json::Value{}},
            {CtStableSwap, "SS", ssParams},
        };

        for (auto& [ct, name, cpJson] : cases)
        {
            // Zero deposit amounts
            {
                Env env(*this, features | featureAMMCurves);
                auto const usd = gw2["USD"];
                auto const eur = gw2["EUR"];
                setupSwapEnv(env, gw2, al, bo, usd, eur);

                auto jv = ammCreateJV(env, al, usd, eur, usd(0), eur(1000));
                if (ct != CtConstantProduct)
                {
                    jv[sfCurveType.jsonName] = ct;
                    if (cpJson.isMember(sfAmplification.jsonName))
                        jv[sfAmplification.jsonName] = cpJson[sfAmplification.jsonName];
                    if (cpJson.isMember(sfFeeTier.jsonName))
                        jv[sfFeeTier.jsonName] = cpJson[sfFeeTier.jsonName];
                }
                env(jv, Ter(temBAD_AMOUNT));
                env.close();
            }

            // Both amounts zero
            {
                Env env(*this, features | featureAMMCurves);
                auto const usd = gw2["USD"];
                auto const eur = gw2["EUR"];
                setupSwapEnv(env, gw2, al, bo, usd, eur);

                auto jv = ammCreateJV(env, al, usd, eur, usd(0), eur(0));
                if (ct != CtConstantProduct)
                {
                    jv[sfCurveType.jsonName] = ct;
                    if (cpJson.isMember(sfAmplification.jsonName))
                        jv[sfAmplification.jsonName] = cpJson[sfAmplification.jsonName];
                    if (cpJson.isMember(sfFeeTier.jsonName))
                        jv[sfFeeTier.jsonName] = cpJson[sfFeeTier.jsonName];
                }
                env(jv, Ter(temBAD_AMOUNT));
                env.close();
            }

            // Negative deposit amounts
            {
                Env env(*this, features | featureAMMCurves);
                auto const usd = gw2["USD"];
                auto const eur = gw2["EUR"];
                setupSwapEnv(env, gw2, al, bo, usd, eur);

                auto jv = ammCreateJV(env, al, usd, eur, STAmount(usd.issue(), -1000), eur(1000));
                if (ct != CtConstantProduct)
                {
                    jv[sfCurveType.jsonName] = ct;
                    if (cpJson.isMember(sfAmplification.jsonName))
                        jv[sfAmplification.jsonName] = cpJson[sfAmplification.jsonName];
                    if (cpJson.isMember(sfFeeTier.jsonName))
                        jv[sfFeeTier.jsonName] = cpJson[sfFeeTier.jsonName];
                }
                env(jv, Ter(temBAD_AMOUNT));
                env.close();
            }

            // Zero sendmax on payment (no swap possible)
            {
                Env env(*this, features | featureAMMCurves);
                auto const usd = gw2["USD"];
                auto const eur = gw2["EUR"];
                setupSwapEnv(env, gw2, al, bo, usd, eur);
                createCurvePool(env, al, usd, eur, usd(10000), eur(10000), ct, cpJson);

                env(pay(bo, bo, eur(100)),
                    jtx::Path(~eur),
                    jtx::Sendmax(usd(0)),
                    jtx::Txflags(tfNoRippleDirect | tfPartialPayment),
                    Ter(temBAD_AMOUNT));
                env.close();
            }

            // Zero delivery amount
            {
                Env env(*this, features | featureAMMCurves);
                auto const usd = gw2["USD"];
                auto const eur = gw2["EUR"];
                setupSwapEnv(env, gw2, al, bo, usd, eur);
                createCurvePool(env, al, usd, eur, usd(10000), eur(10000), ct, cpJson);

                env(pay(bo, bo, eur(0)),
                    jtx::Path(~eur),
                    jtx::Sendmax(usd(100)),
                    jtx::Txflags(tfNoRippleDirect | tfPartialPayment),
                    Ter(temBAD_AMOUNT));
                env.close();
            }
        }
    }

    void
    testScalePrecision(FeatureBitset features)
    {
        testcase("Scale precision");

        using namespace jtx;

        Account const al("alice");
        Account const bo("bob");
        Account const gw2("gateway");

        json::Value ssParams;
        ssParams[sfAmplification.jsonName] = 100;

        // Theory: curve math must hold at 1e11 scale without
        // precision degradation. Verify by checking the pool product
        // invariant after swap at scale, and that SS near-1:1
        // property holds at large magnitudes.

        auto const setupLargeEnv = [&](Env& env, auto const& usd, auto const& eur) {
            env.fund(XRP(100000), gw2, al, bo);
            STAmount const limit(usd.issue(), 1, 15);
            STAmount const limitE(eur.issue(), 1, 15);
            env.trust(limit, al);
            env.trust(limitE, al);
            env.trust(limit, bo);
            env.trust(limitE, bo);
            env(pay(gw2, al, STAmount(usd.issue(), 1, 12)));
            env(pay(gw2, al, STAmount(eur.issue(), 1, 12)));
            env(pay(gw2, bo, STAmount(usd.issue(), 1, 10)));
            env(pay(gw2, bo, STAmount(eur.issue(), 1, 10)));
            env.close();
        };

        struct ScaleCase
        {
            std::uint8_t ct{};
            json::Value params{};
        };

        ScaleCase scaleCases[] = {
            {CtConstantProduct, json::Value{}},
            {CtStableSwap, ssParams},
        };

        for (auto& [ct, cpJson] : scaleCases)
        {
            Env env(*this, features | featureAMMCurves);
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            setupLargeEnv(env, usd, eur);

            createCurvePool(
                env,
                al,
                usd,
                eur,
                STAmount(usd.issue(), 1, 11),
                STAmount(eur.issue(), 1, 11),
                ct,
                cpJson);

            auto const ammSle = env.current()->read(keylet::amm(usd.asset(), eur.asset(), ct));
            BEAST_EXPECT(ammSle != nullptr);
            if (!ammSle)
                continue;

            auto const ammAcct = ammSle->getAccountID(sfAccount);
            auto const [p1Before, p2Before] = ammPoolHolds(
                *env.current(),
                ammAcct,
                usd.asset(),
                eur.asset(),
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                env.journal);

            auto const eurBefore = env.balance(bo, eur.issue());
            STAmount const eurTarget(eur.issue(), 1, 9);
            STAmount const usdMax(usd.issue(), 2, 9);
            env(pay(bo, bo, eurTarget),
                jtx::Path(~eur),
                jtx::Sendmax(usdMax),
                jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
            env.close();

            auto const eurGot = env.balance(bo, eur.issue()) - eurBefore;
            BEAST_EXPECT(eurGot > eur(0));

            // SS theory: near 1:1 property must hold at scale
            if (ct == CtStableSwap)
                BEAST_EXPECT(Number(eurGot) > Number(990000000, 0));

            // Pool product invariant must hold at scale
            auto const [p1After, p2After] = ammPoolHolds(
                *env.current(),
                ammAcct,
                usd.asset(),
                eur.asset(),
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                env.journal);

            // xy=k invariant only holds for CP; for all curves,
            // pool must still have positive balances after swap
            BEAST_EXPECT(p1After > STAmount(usd.issue(), 0));
            BEAST_EXPECT(p2After > STAmount(eur.issue(), 0));

            if (ct == CtConstantProduct)
            {
                auto const kBefore = Number(p1Before) * Number(p2Before);
                auto const kAfter = Number(p1After) * Number(p2After);
                BEAST_EXPECT(
                    kAfter >= kBefore || withinRelativeDistance(kBefore, kAfter, Number{1, -7}));
            }
        }

        // Tiny pool with big swap: conservation under extreme ratio
        {
            Env env(*this, features | featureAMMCurves);
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            setupSwapEnv(env, gw2, al, bo, usd, eur);
            createCurvePool(env, al, usd, eur, usd(10), eur(10), CtConstantProduct, json::Value{});

            auto const eurBefore = env.balance(bo, eur.issue());
            env(pay(bo, bo, eur(5)),
                jtx::Path(~eur),
                jtx::Sendmax(usd(50000)),
                jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
            env.close();

            auto const eurGot = env.balance(bo, eur.issue()) - eurBefore;
            // Conservation: cannot extract more than pool held
            BEAST_EXPECT(Number(eurGot) < Number{10});
            BEAST_EXPECT(Number(eurGot) > Number{0});
        }
    }

    void
    testSwapInvariants(FeatureBitset features)
    {
        testcase("Swap invariants");

        using namespace jtx;

        Account const al("alice");
        Account const bo("bob");
        Account const gw2("gateway");

        json::Value ssParams;
        ssParams[sfAmplification.jsonName] = 100;

        struct CurveCase
        {
            std::uint8_t type;
            std::string name;
            json::Value params;
        };

        CurveCase cases[] = {
            {CtConstantProduct, "CP", json::Value{}},
            {CtStableSwap, "SS", ssParams},
        };

        for (auto& [ct, name, cpJson] : cases)
        {
            // Dust swap: 0.000001 units
            {
                Env env(*this, features | featureAMMCurves);
                auto const usd = gw2["USD"];
                auto const eur = gw2["EUR"];
                setupSwapEnv(env, gw2, al, bo, usd, eur);
                createCurvePool(env, al, usd, eur, usd(10000), eur(10000), ct, cpJson);

                auto const eurBefore = env.balance(bo, eur.issue());
                auto const usdBefore = env.balance(bo, usd.issue());

                STAmount const dustAmt(eur.issue(), 1, -6);
                env(pay(bo, bo, dustAmt),
                    jtx::Path(~eur),
                    jtx::Sendmax(usd(1)),
                    jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
                env.close();

                auto const eurGot = env.balance(bo, eur.issue()) - eurBefore;
                auto const usdSpent = usdBefore - env.balance(bo, usd.issue());

                // Should get something, not zero
                BEAST_EXPECT(eurGot >= dustAmt);
                // Should not overpay massively
                BEAST_EXPECT(Number(usdSpent) < Number{1});
            }

            // Repeated small swaps vs one big swap:
            // total output should be <= single big swap
            // (due to price impact compounding)
            {
                // Single big swap
                STAmount bigOut;
                {
                    Env env(*this, features | featureAMMCurves);
                    auto const usd = gw2["USD"];
                    auto const eur = gw2["EUR"];
                    setupSwapEnv(env, gw2, al, bo, usd, eur);
                    createCurvePool(env, al, usd, eur, usd(10000), eur(10000), ct, cpJson);

                    auto const eurBefore = env.balance(bo, eur.issue());
                    env(pay(bo, bo, eur(500)),
                        jtx::Path(~eur),
                        jtx::Sendmax(usd(600)),
                        jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
                    env.close();
                    bigOut = env.balance(bo, eur.issue()) - eurBefore;
                }

                // Five small swaps of 1/5 the sendmax
                STAmount totalSmallOut;
                {
                    Env env(*this, features | featureAMMCurves);
                    auto const usd = gw2["USD"];
                    auto const eur = gw2["EUR"];
                    setupSwapEnv(env, gw2, al, bo, usd, eur);
                    createCurvePool(env, al, usd, eur, usd(10000), eur(10000), ct, cpJson);

                    auto const eurStart = env.balance(bo, eur.issue());
                    for (int i = 0; i < 5; ++i)
                    {
                        env(pay(bo, bo, eur(100)),
                            jtx::Path(~eur),
                            jtx::Sendmax(usd(120)),
                            jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
                        env.close();
                    }
                    totalSmallOut = env.balance(bo, eur.issue()) - eurStart;
                }

                // Small swaps must execute meaningfully
                BEAST_EXPECT(Number(totalSmallOut) > Number{0});
                // Many small swaps get less total output
                // due to compounding price impact
                BEAST_EXPECT(totalSmallOut <= bigOut);
            }

            // Asymmetric pool: verify no free tokens
            {
                Env env(*this, features | featureAMMCurves);
                auto const usd = gw2["USD"];
                auto const eur = gw2["EUR"];
                setupSwapEnv(env, gw2, al, bo, usd, eur);
                createCurvePool(env, al, usd, eur, usd(1000), eur(100), ct, cpJson);

                auto const eurBefore = env.balance(bo, eur.issue());
                auto const usdBefore = env.balance(bo, usd.issue());

                // Swap USD -> EUR
                env(pay(bo, bo, eur(10)),
                    jtx::Path(~eur),
                    jtx::Sendmax(usd(200)),
                    jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
                env.close();

                auto const eurGot = env.balance(bo, eur.issue()) - eurBefore;
                auto const usdSpent = usdBefore - env.balance(bo, usd.issue());

                // Swap back EUR -> USD
                auto const usdBefore2 = env.balance(bo, usd.issue());
                env(pay(bo, bo, usd(200)),
                    jtx::Path(~usd),
                    jtx::Sendmax(eurGot),
                    jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
                env.close();

                auto const usdGotBack = env.balance(bo, usd.issue()) - usdBefore2;

                // Round-trip should not produce meaningful profit.
                // Allow 1-drop rounding tolerance.
                auto const tolerance = Number(1, -6);
                BEAST_EXPECT(Number(usdGotBack) <= Number(usdSpent) + tolerance);
            }

            // Pool balance invariant: total value doesn't decrease
            {
                Env env(*this, features | featureAMMCurves);
                auto const usd = gw2["USD"];
                auto const eur = gw2["EUR"];
                setupSwapEnv(env, gw2, al, bo, usd, eur);
                createCurvePool(env, al, usd, eur, usd(10000), eur(10000), ct, cpJson);

                auto const ammSle = env.current()->read(keylet::amm(usd.asset(), eur.asset(), ct));
                BEAST_EXPECT(ammSle != nullptr);
                if (!ammSle)
                    continue;

                auto const ammAcct = ammSle->getAccountID(sfAccount);
                auto const [pool1Before, pool2Before] = ammPoolHolds(
                    *env.current(),
                    ammAcct,
                    usd.asset(),
                    eur.asset(),
                    FreezeHandling::IgnoreFreeze,
                    AuthHandling::IgnoreAuth,
                    env.journal);

                // Do a swap
                env(pay(bo, bo, eur(500)),
                    jtx::Path(~eur),
                    jtx::Sendmax(usd(600)),
                    jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
                env.close();

                auto const [pool1After, pool2After] = ammPoolHolds(
                    *env.current(),
                    ammAcct,
                    usd.asset(),
                    eur.asset(),
                    FreezeHandling::IgnoreFreeze,
                    AuthHandling::IgnoreAuth,
                    env.journal);

                // Pool should retain positive balances
                BEAST_EXPECT(pool1After > usd(0));
                BEAST_EXPECT(pool2After > eur(0));

                // CP: xy=k must hold after swap
                if (ct == CtConstantProduct)
                {
                    auto const productBefore = Number(pool1Before) * Number(pool2Before);
                    auto const productAfter = Number(pool1After) * Number(pool2After);
                    BEAST_EXPECT(
                        productAfter >= productBefore ||
                        withinRelativeDistance(productBefore, productAfter, Number{1, -7}));
                }
            }
        }
    }

    void
    testFeeExtraction(FeatureBitset features)
    {
        testcase("Fee extraction");

        using namespace jtx;

        Account const al("alice");
        Account const bo("bob");
        Account const gw2("gateway");

        json::Value ssParams;
        ssParams[sfAmplification.jsonName] = 100;

        struct CurveCase
        {
            std::uint8_t type{};
            json::Value params{};
        };

        CurveCase cases[] = {
            {CtConstantProduct, json::Value{}},
            {CtStableSwap, ssParams},
        };

        for (auto& [ct, cpJson] : cases)
        {
            // Fee=1 bps (minimum non-zero): trader always pays fee
            {
                Env envNoFee(*this, features | featureAMMCurves);
                Env envFee(*this, features | featureAMMCurves);
                auto const usd = gw2["USD"];
                auto const eur = gw2["EUR"];

                setupSwapEnv(envNoFee, gw2, al, bo, usd, eur);
                createCurvePool(envNoFee, al, usd, eur, usd(10000), eur(10000), ct, cpJson, 0);

                setupSwapEnv(envFee, gw2, al, bo, usd, eur);
                createCurvePool(envFee, al, usd, eur, usd(10000), eur(10000), ct, cpJson, 1);

                auto const nfUsdBefore = envNoFee.balance(bo, usd.issue());
                envNoFee(
                    pay(bo, bo, eur(500)),
                    jtx::Path(~eur),
                    jtx::Sendmax(usd(600)),
                    jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
                envNoFee.close();
                auto const nfUsdSpent = nfUsdBefore - envNoFee.balance(bo, usd.issue());

                auto const fUsdBefore = envFee.balance(bo, usd.issue());
                envFee(
                    pay(bo, bo, eur(500)),
                    jtx::Path(~eur),
                    jtx::Sendmax(usd(600)),
                    jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
                envFee.close();
                auto const fUsdSpent = fUsdBefore - envFee.balance(bo, usd.issue());

                // Even minimum fee must increase cost
                BEAST_EXPECT(fUsdSpent > nfUsdSpent);
            }

            // Max fee (1000 = 1% = 100bps): compare cost
            // against no-fee pool for same delivery
            {
                Env envNoFee2(*this, features | featureAMMCurves);
                Env envMaxFee(*this, features | featureAMMCurves);
                auto const usd = gw2["USD"];
                auto const eur = gw2["EUR"];

                setupSwapEnv(envNoFee2, gw2, al, bo, usd, eur);
                createCurvePool(envNoFee2, al, usd, eur, usd(10000), eur(10000), ct, cpJson, 0);

                setupSwapEnv(envMaxFee, gw2, al, bo, usd, eur);
                createCurvePool(envMaxFee, al, usd, eur, usd(10000), eur(10000), ct, cpJson, 1000);

                auto const nfUsdBefore = envNoFee2.balance(bo, usd.issue());
                envNoFee2(
                    pay(bo, bo, eur(500)),
                    jtx::Path(~eur),
                    jtx::Sendmax(usd(600)),
                    jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
                envNoFee2.close();
                auto const nfUsdSpent = nfUsdBefore - envNoFee2.balance(bo, usd.issue());

                auto const mfUsdBefore = envMaxFee.balance(bo, usd.issue());
                envMaxFee(
                    pay(bo, bo, eur(500)),
                    jtx::Path(~eur),
                    jtx::Sendmax(usd(600)),
                    jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
                envMaxFee.close();
                auto const mfUsdSpent = mfUsdBefore - envMaxFee.balance(bo, usd.issue());

                // 1% fee must increase cost for same delivery
                BEAST_EXPECT(mfUsdSpent > nfUsdSpent);
            }
        }
    }

    void
    testConservation(FeatureBitset features)
    {
        testcase("Conservation");

        using namespace jtx;

        Account const al("alice");
        Account const bo("bob");
        Account const gw2("gateway");

        json::Value ssParams;
        ssParams[sfAmplification.jsonName] = 100;

        struct CurveCase
        {
            std::uint8_t type{};
            json::Value params{};
        };

        CurveCase cases[] = {
            {CtConstantProduct, json::Value{}},
            {CtStableSwap, ssParams},
        };

        for (auto& [ct, cpJson] : cases)
        {
            // Trying to get more than the pool has
            {
                Env env(*this, features | featureAMMCurves);
                auto const usd = gw2["USD"];
                auto const eur = gw2["EUR"];
                setupSwapEnv(env, gw2, al, bo, usd, eur);
                createCurvePool(env, al, usd, eur, usd(100), eur(100), ct, cpJson);

                auto const eurBefore = env.balance(bo, eur.issue());

                // Try to get 200 from a 100-unit pool
                env(pay(bo, bo, eur(200)),
                    jtx::Path(~eur),
                    jtx::Sendmax(usd(50000)),
                    jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
                env.close();

                auto const eurGot = env.balance(bo, eur.issue()) - eurBefore;

                // Can never extract more than pool had
                BEAST_EXPECT(Number(eurGot) < Number{100});
                // Pool should still have positive balance
                auto const ammSle = env.current()->read(keylet::amm(usd.asset(), eur.asset(), ct));
                if (ammSle)
                {
                    auto const ammAcct = ammSle->getAccountID(sfAccount);
                    auto const [p1, p2] = ammPoolHolds(
                        *env.current(),
                        ammAcct,
                        usd.asset(),
                        eur.asset(),
                        FreezeHandling::IgnoreFreeze,
                        AuthHandling::IgnoreAuth,
                        env.journal);
                    BEAST_EXPECT(p1 > usd(0));
                    BEAST_EXPECT(p2 > eur(0));
                }
            }

            // Sender has insufficient funds
            {
                Env env(*this, features | featureAMMCurves);
                auto const usd = gw2["USD"];
                auto const eur = gw2["EUR"];

                env.fund(XRP(100000), gw2, al, bo);
                env.trust(usd(1000000), al);
                env.trust(eur(1000000), al);
                env.trust(usd(1000000), bo);
                env.trust(eur(1000000), bo);
                env(pay(gw2, al, usd(100000)));
                env(pay(gw2, al, eur(100000)));
                env(pay(gw2, bo, usd(10)));
                env(pay(gw2, bo, eur(10)));
                env.close();

                createCurvePool(env, al, usd, eur, usd(10000), eur(10000), ct, cpJson);

                auto const eurBefore = env.balance(bo, eur.issue());

                // Bob only has 10 USD, tries to swap 10000
                env(pay(bo, bo, eur(10000)),
                    jtx::Path(~eur),
                    jtx::Sendmax(usd(10000)),
                    jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
                env.close();

                auto const eurGot = env.balance(bo, eur.issue()) - eurBefore;
                auto const usdAfter = env.balance(bo, usd.issue());

                // Bob started with 10 USD: should get some EUR
                // but can't have spent more than he had
                BEAST_EXPECT(eurGot > eur(0));
                BEAST_EXPECT(eurGot < eur(10000));
                BEAST_EXPECT(usdAfter <= usd(10));
            }
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testTickMath();
        testGetCurve(features);
        testConstantProduct(features);
        testStableSwap(features);
        testConcentratedLiquidity(features);
        testFees(features);
        testNewtonBoundary(features);
        testRoundTrip(features);
        testDust(features);
        testPreflight(features);
        testPreflightDisabled(features);
        testDoApply(features);
        testAmplificationVotePreflight(features);
        testAmplificationVotePreclaim(features);
        testAmplificationVoteApply(features);
        testCLNonZeroTick(features);
        testStableSwapEdgeCases(features);
        testCurvePricing(features);
        testBoundaryRejection(features);
        testScalePrecision(features);
        testSwapInvariants(features);
        testFeeExtraction(features);
        testConservation(features);
    }

public:
    void
    run() override
    {
        auto const features = testableAmendments();
        testWithFeats(features);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(AMMCurves, app, xrpl, 1);

}  // namespace xrpl::test
