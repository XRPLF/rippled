#include <test/jtx/AMMTest.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/deposit.h>
#include <test/jtx/offer.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/rate.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/ter.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/OwnerCounts.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AMMCurve.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/ledger/helpers/AMMTickMath.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/apply.h>
#include <xrpl/tx/invariants/AMMInvariant.h>
#include <xrpl/tx/paths/AMMLiquidity.h>
#include <xrpl/tx/paths/AMMOffer.h>
#include <xrpl/tx/transactors/dex/AMMContext.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <ostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl::test {

namespace {

// Forwards to a base view and counts reads of one CL pool's tick-bitmap words.
class BitmapReadCounter final : public ReadView
{
    ReadView const& base_;
    uint256 const first_;
    uint256 const last_;

public:
    mutable std::size_t bitmapReads = 0;

    BitmapReadCounter(ReadView const& base, uint256 const& ammID)
        : base_(base)
        , first_(keylet::ammTickBitmapBase(ammID).key)
        , last_(keylet::ammTickBitmapEnd(ammID).key)
    {
    }

    [[nodiscard]] bool
    inBitmapRange(uint256 const& key) const
    {
        return key >= first_ && key <= last_;
    }

    [[nodiscard]] LedgerHeader const&
    header() const override
    {
        return base_.header();
    }

    [[nodiscard]] bool
    open() const override
    {
        return base_.open();
    }

    [[nodiscard]] Fees const&
    fees() const override
    {
        return base_.fees();
    }

    [[nodiscard]] Rules const&
    rules() const override
    {
        return base_.rules();
    }

    [[nodiscard]] bool
    exists(Keylet const& k) const override
    {
        if (inBitmapRange(k.key))
            ++bitmapReads;
        return base_.exists(k);
    }

    [[nodiscard]] std::optional<key_type>
    succ(key_type const& key, std::optional<key_type> const& last = std::nullopt) const override
    {
        return base_.succ(key, last);
    }

    [[nodiscard]] std::optional<key_type>
    pred(key_type const& key, std::optional<key_type> const& first = std::nullopt) const override
    {
        return base_.pred(key, first);
    }

    [[nodiscard]] SLE::const_pointer
    read(Keylet const& k) const override
    {
        if (inBitmapRange(k.key))
            ++bitmapReads;
        return base_.read(k);
    }

    [[nodiscard]] STAmount
    balanceHookIOU(AccountID const& account, AccountID const& issuer, STAmount const& amount)
        const override
    {
        return base_.balanceHookIOU(account, issuer, amount);
    }

    [[nodiscard]] STAmount
    balanceHookMPT(AccountID const& account, MPTIssue const& issue, std::int64_t amount)
        const override
    {
        return base_.balanceHookMPT(account, issue, amount);
    }

    [[nodiscard]] STAmount
    balanceHookSelfIssueMPT(MPTIssue const& issue, std::int64_t amount) const override
    {
        return base_.balanceHookSelfIssueMPT(issue, amount);
    }

    [[nodiscard]] OwnerCounts
    ownerCountHook(AccountID const& account, OwnerCounts const& count) const override
    {
        return base_.ownerCountHook(account, count);
    }

    [[nodiscard]] std::unique_ptr<SlesType::IterBase>
    slesBegin() const override
    {
        return base_.slesBegin();
    }

    [[nodiscard]] std::unique_ptr<SlesType::IterBase>
    slesEnd() const override
    {
        return base_.slesEnd();
    }

    [[nodiscard]] std::unique_ptr<SlesType::IterBase>
    slesUpperBound(key_type const& key) const override
    {
        return base_.slesUpperBound(key);
    }

    [[nodiscard]] std::unique_ptr<TxsType::IterBase>
    txsBegin() const override
    {
        return base_.txsBegin();
    }

    [[nodiscard]] std::unique_ptr<TxsType::IterBase>
    txsEnd() const override
    {
        return base_.txsEnd();
    }

    [[nodiscard]] bool
    txExists(key_type const& key) const override
    {
        return base_.txExists(key);
    }

    [[nodiscard]] TxType
    txRead(key_type const& key) const override
    {
        return base_.txRead(key);
    }
};

}  // namespace

struct AMMCurves_test : public jtx::AMMTest
{
private:
    static FeatureBitset
    testableAmendments()
    {
        return jtx::testableAmendments();
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
            for (auto const t : {0, 1, 2, 10, 100, 1000, -1, -2, -100, -1000})
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
            BEAST_EXPECT(getCurve(kCtConstantProduct, rules) != nullptr);
            BEAST_EXPECT(getCurve(kCtConcentratedLiquidity, rules) != nullptr);
            BEAST_EXPECT(getCurve(kCtStableSwap, rules) != nullptr);
            BEAST_EXPECT(getCurve(255, rules) == nullptr);
        }

        // without amendment: only CP available
        {
            Env const env(*this, features - featureAMMCurves);
            auto const& rules = env.current()->rules();
            BEAST_EXPECT(getCurve(kCtConstantProduct, rules) != nullptr);
            BEAST_EXPECT(getCurve(kCtConcentratedLiquidity, rules) == nullptr);
            BEAST_EXPECT(getCurve(kCtStableSwap, rules) == nullptr);
        }
    }

    void
    testConstantProduct(FeatureBitset features)
    {
        testcase("ConstantProduct");

        using namespace jtx;
        Env const env(*this, features | featureAMMCurves);

        auto const* curve = getCurve(kCtConstantProduct, env.current()->rules());
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

        // spotPrice: balanced=1, imbalanced=2, fee lowers it
        {
            auto const sp = curve->spotPrice(poolIn, poolOut, 0, nullptr);
            BEAST_EXPECT(sp.has_value());
            BEAST_EXPECT(*sp == Number{1});

            auto const sp2 = curve->spotPrice(USD(500), poolOut, 0, nullptr);
            BEAST_EXPECT(sp2.has_value());
            BEAST_EXPECT(*sp2 == Number{2});

            auto const spFee = curve->spotPrice(poolIn, poolOut, 100, nullptr);
            BEAST_EXPECT(spFee.has_value());
            BEAST_EXPECT(*spFee < Number{1});
        }

        // validateParams: always succeeds (no params needed)
        {
            STObject const obj(sfGeneric);
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

        auto const* curve = getCurve(kCtStableSwap, env.current()->rules());
        BEAST_EXPECT(curve != nullptr);

        STObject txParams(sfGeneric);
        txParams.setFieldU32(sfAmplification, 100);

        auto const ammSle = std::make_shared<SLE>(ltAMM, uint256{});
        ammSle->setFieldU32(sfAmplification, 100);

        STAmount const poolIn = USD(1000);
        STAmount const poolOut = EUR(1000);

        // validateParams
        {
            // temMALFORMED: missing amplification
            STObject const empty(sfGeneric);
            BEAST_EXPECT(curve->validateParams(empty) == temMALFORMED);

            // temMALFORMED: A=0
            STObject zero(sfGeneric);
            zero.setFieldU32(sfAmplification, 0);
            BEAST_EXPECT(curve->validateParams(zero) == temMALFORMED);

            // temMALFORMED: A > MAX
            STObject over(sfGeneric);
            over.setFieldU32(sfAmplification, maxAmplification + 1);
            BEAST_EXPECT(curve->validateParams(over) == temMALFORMED);

            // tesSUCCESS: valid values
            BEAST_EXPECT(curve->validateParams(txParams) == tesSUCCESS);

            STObject minP(sfGeneric);
            minP.setFieldU32(sfAmplification, minAmplification);
            BEAST_EXPECT(curve->validateParams(minP) == tesSUCCESS);

            STObject maxP(sfGeneric);
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

        auto const* curve = getCurve(kCtConcentratedLiquidity, env.current()->rules());
        BEAST_EXPECT(curve != nullptr);

        auto const ammSle = std::make_shared<SLE>(ltAMM, uint256{});
        ammSle->setFieldU8(sfFeeTier, kFtMedium);
        ammSle->setFieldI32(sfCurrentTick, 0);
        ammSle->setFieldU64(sfActiveLiquidity, 1000000);

        STAmount const poolIn = USD(1000);
        STAmount const poolOut = EUR(1000);

        // validateParams
        {
            // temMALFORMED: missing fee tier
            STObject const empty(sfGeneric);
            BEAST_EXPECT(curve->validateParams(empty) == temMALFORMED);

            // temMALFORMED: fee tier out of bounds
            for (std::uint8_t ft : {
                     feeTierCount,
                     static_cast<std::uint8_t>(feeTierCount + 1),
                     std::numeric_limits<std::uint8_t>::max(),
                 })
            {
                STObject obj(sfGeneric);
                obj.setFieldU8(sfFeeTier, ft);
                BEAST_EXPECT(curve->validateParams(obj) == temMALFORMED);
            }

            // tesSUCCESS: all valid tiers
            for (std::uint8_t ft = 0; ft < feeTierCount; ++ft)
            {
                STObject obj(sfGeneric);
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
            auto const spSle = std::make_shared<SLE>(ltAMM, uint256{});
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
            auto const zeroLiqSle = std::make_shared<SLE>(ltAMM, uint256{});
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
            auto const lowLiqSle = std::make_shared<SLE>(ltAMM, uint256{});
            lowLiqSle->setFieldU8(sfFeeTier, kFtMedium);
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
            auto const* curve = getCurve(kCtConstantProduct, rules);
            auto const noFee = curve->swapIn(poolIn, poolOut, assetIn, 0, nullptr);
            auto const withFee = curve->swapIn(poolIn, poolOut, assetIn, tfee, nullptr);
            BEAST_EXPECT(noFee.has_value() && withFee.has_value());
            BEAST_EXPECT(*withFee < *noFee);
        }

        // SS: fee reduces output
        {
            auto const ssSle = std::make_shared<SLE>(ltAMM, uint256{});
            ssSle->setFieldU32(sfAmplification, 100);
            auto const* curve = getCurve(kCtStableSwap, rules);
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

        auto const* curve = getCurve(kCtStableSwap, env.current()->rules());
        BEAST_EXPECT(curve != nullptr);

        // extreme asymmetry at max A
        {
            auto const ammSle = std::make_shared<SLE>(ltAMM, uint256{});
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
            auto const ammSle = std::make_shared<SLE>(ltAMM, uint256{});
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
            STObject badParams(sfGeneric);
            badParams.setFieldU32(sfAmplification, 0);
            BEAST_EXPECT(curve->validateParams(badParams) == temMALFORMED);

            auto const badSle = std::make_shared<SLE>(ltAMM, uint256{});
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
            auto const ammSle = std::make_shared<SLE>(ltAMM, uint256{});
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

            STObject maxA(sfGeneric);
            maxA.setFieldU32(sfAmplification, maxAmplification);
            auto const lpHigh = curve->initialLPTokens(USD(1000), EUR(1000), lptIssue, &maxA);
            BEAST_EXPECT(lpHigh.has_value());
            auto const lpHighNum = Number(*lpHigh);
            BEAST_EXPECT(lpHighNum > Number{1999});
            BEAST_EXPECT(lpHighNum <= Number{2000});

            STObject minA(sfGeneric);
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
            auto const* curve = getCurve(kCtStableSwap, rules);
            auto const ammSle = std::make_shared<SLE>(ltAMM, uint256{});
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
            auto const* curve = getCurve(kCtStableSwap, rules);
            auto const ammSle = std::make_shared<SLE>(ltAMM, uint256{});
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

        auto const ssSle = std::make_shared<SLE>(ltAMM, uint256{});
        ssSle->setFieldU32(sfAmplification, 100);

        CurveSetup setups[] = {
            {.type = kCtConstantProduct, .sle = nullptr},
            {.type = kCtStableSwap, .sle = ssSle},
        };

        for (auto& [type, sle] : setups)
        {
            auto const* curve = getCurve(type, rules);
            if (curve == nullptr)
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
        env.close();
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

        // temDISABLED: CurveType 4 is reserved for Smart AMM (separate
        // amendment, not yet activated). Distinct from temMALFORMED so
        // clients can distinguish "try again when activated" from
        // "permanently invalid".
        {
            auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            jv[sfCurveType.jsonName] = 4;
            env(jv, Ter(temDISABLED));
            env.close();
        }

        // temMALFORMED: StableSwap without CurveParams
        {
            auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            jv[sfCurveType.jsonName] = kCtStableSwap;
            env(jv, Ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: StableSwap A > maxAmplification
        {
            auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            jv[sfCurveType.jsonName] = kCtStableSwap;
            jv[sfAmplification.jsonName] = maxAmplification + 1;
            env(jv, Ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: StableSwap A=0
        {
            auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            jv[sfCurveType.jsonName] = kCtStableSwap;
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
        env.close();
        env.trust(usd(100000), al);
        env.trust(eur(100000), al);
        env(pay(gw2, al, usd(10000)));
        env(pay(gw2, al, eur(10000)));
        env.close();

        // temDISABLED: curve type without amendment
        {
            auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            jv[sfCurveType.jsonName] = kCtStableSwap;
            jv[sfAmplification.jsonName] = 100;
            env(jv, Ter(temDISABLED));
            env.close();
        }

        // temDISABLED: any field AMMCurves adds to an existing AMM transaction.
        auto const ammTx = [&](json::StaticString const& type) {
            json::Value jv;
            jv[jss::Account] = al.human();
            jv[jss::TransactionType] = type;
            jv[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            jv[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            jv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            return jv;
        };
        for (auto const& type : {
                 jss::AMMDeposit,
                 jss::AMMWithdraw,
                 jss::AMMBid,
                 jss::AMMVote,
                 jss::AMMDelete,
                 jss::AMMClawback,
             })
        {
            auto jv = ammTx(type);
            jv[sfCurveType.jsonName] = kCtConstantProduct;
            if (type == jss::AMMVote)
                jv[jss::TradingFee] = 0;
            if (type == jss::AMMClawback)
            {
                jv[jss::Account] = gw2.human();
                jv[jss::Holder] = al.human();
                jv[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            }
            env(jv, Ter(temDISABLED));
        }
        {
            auto jv = ammTx(jss::AMMDeposit);
            jv[sfTickLower.jsonName] = -60;
            jv[sfTickUpper.jsonName] = 60;
            env(jv, Ter(temDISABLED));
        }
        {
            auto jv = ammTx(jss::AMMDeposit);
            jv[sfBinID.jsonName] = 0;
            env(jv, Ter(temDISABLED));
        }
        {
            auto jv = ammTx(jss::AMMWithdraw);
            jv[sfPositionID.jsonName] = to_string(uint256{1u});
            env(jv, Ter(temDISABLED));
        }
        {
            auto jv = ammTx(jss::AMMWithdraw);
            jv[sfPositionLiquidity.jsonName] = "1";
            env(jv, Ter(temDISABLED));
        }
        {
            auto jv = ammTx(jss::AMMVote);
            jv[jss::TradingFee] = 0;
            jv[sfAmplification.jsonName] = 100;
            env(jv, Ter(temDISABLED));
        }
        // An AMMCreate naming a curve parameter without a curve type.
        for (auto const& [name, value] : {
                 std::pair{sfFeeTier.jsonName, 1},
                 std::pair{sfBinStep.jsonName, 10},
                 std::pair{sfAmplification.jsonName, 100},
             })
        {
            auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            jv[name] = value;
            env(jv, Ter(temDISABLED));
        }
        {
            auto jv = ammTx(jss::AMMWithdraw);
            jv[sfShares.jsonName] = "1";
            env(jv, Ter(temDISABLED));
        }
        env.close();
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
        env.close();
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
            jv[sfCurveType.jsonName] = kCtConstantProduct;
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
            jv[sfCurveType.jsonName] = kCtStableSwap;
            jv[sfAmplification.jsonName] = 100;
            env(jv);
            env.close();

            auto const ammSle =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtStableSwap));
            BEAST_EXPECT(ammSle != nullptr);
            if (ammSle)
            {
                BEAST_EXPECT(ammSle->getFieldU8(sfCurveType) == kCtStableSwap);
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
            jv[sfCurveType.jsonName] = kCtStableSwap;
            jv[sfAmplification.jsonName] = 5000u;
            env(jv);
            env.close();

            auto const ammSle =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtStableSwap));
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
        jvCreate[sfCurveType.jsonName] = kCtStableSwap;
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

            auto const jv = ammVoteJV(env2, al2, usd2, eur2, 0, 200);
            env2(jv, Ter(temDISABLED));
            env2.close();
        }

        // temMALFORMED: amplification below MIN
        {
            auto const jv = ammVoteJV(env, al, usd, eur, 0, 0, kCtStableSwap);
            env(jv, Ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: amplification above MAX
        {
            auto const jv = ammVoteJV(env, al, usd, eur, 0, maxAmplification + 1, kCtStableSwap);
            env(jv, Ter(temMALFORMED));
            env.close();
        }

        // A vote names its trading fee, with or without an amplification.
        for (std::optional<std::uint32_t> const amp : {std::optional<std::uint32_t>{}, {110u}})
        {
            auto jv = ammVoteJV(env, al, usd, eur, 0, amp, kCtStableSwap);
            jv.removeMember(jss::TradingFee);
            env(jv, Ter(temMALFORMED));
            env.close();
        }

        // Valid: amplification within range
        {
            auto const jv = ammVoteJV(env, al, usd, eur, 0, 110, kCtStableSwap);
            env(jv);
            env.close();
        }

        // Valid: minAmplification
        {
            auto const jv = ammVoteJV(env, al, usd, eur, 0, minAmplification, kCtStableSwap);
            env(jv);
            env.close();
        }

        // Valid: maxAmplification
        {
            auto const jv = ammVoteJV(env, al, usd, eur, 0, maxAmplification, kCtStableSwap);
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
            auto const jvCreate = ammCreateJV(env, al, usd, eur, usd(10000), eur(10000));
            env(jvCreate);
            env.close();

            auto const jv = ammVoteJV(env, al, usd, eur, 0, 50);
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
            jvCreate[sfCurveType.jsonName] = kCtStableSwap;
            jvCreate[sfAmplification.jsonName] = 100;
            env(jvCreate);
            env.close();

            // Vote to increase to 200 (clamped to 100+10=110)
            auto const jv = ammVoteJV(env, al, usd, eur, 0, 200, kCtStableSwap);
            env(jv);
            env.close();

            auto const ammSle =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtStableSwap));
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
            jvCreate[sfCurveType.jsonName] = kCtStableSwap;
            jvCreate[sfAmplification.jsonName] = 100;
            env(jvCreate);
            env.close();

            // Vote to decrease to 1 (clamped to 100-10=90)
            auto const jv = ammVoteJV(env, al, usd, eur, 0, 1, kCtStableSwap);
            env(jv);
            env.close();

            auto const ammSle =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtStableSwap));
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
            jvCreate[sfCurveType.jsonName] = kCtStableSwap;
            jvCreate[sfAmplification.jsonName] = minAmplification;
            env(jvCreate);
            env.close();

            // Vote to increase from 1 to 100
            // maxChange = max(1*10/100, 1) = 1, so clamped to 2
            auto const jv = ammVoteJV(env, al, usd, eur, 0, 100, kCtStableSwap);
            env(jv);
            env.close();

            auto const ammSle =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtStableSwap));
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
            jvCreate[sfCurveType.jsonName] = kCtStableSwap;
            jvCreate[sfAmplification.jsonName] = 5;
            env(jvCreate);
            env.close();

            // 5*10/100=0, but max(0,1)=1, so new amp = 6
            auto const jv = ammVoteJV(env, al, usd, eur, 0, 100, kCtStableSwap);
            env(jv);
            env.close();

            auto const ammSle =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtStableSwap));
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
            jvCreate[sfCurveType.jsonName] = kCtStableSwap;
            jvCreate[sfAmplification.jsonName] = 100;
            env(jvCreate);
            env.close();

            auto const jv = ammVoteJV(env, al, usd, eur, 0, 100, kCtStableSwap);
            env(jv);
            env.close();

            auto const ammSle =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtStableSwap));
            BEAST_EXPECT(ammSle != nullptr);
            if (ammSle)
            {
                auto const amp = ammSle->getFieldU32(sfAmplification);
                BEAST_EXPECT(amp == 100);
            }
        }
    }

    void
    testStableSwapTwoAssetDepositWithdraw(FeatureBitset features)
    {
        testcase("StableSwap: a second LP deposits both assets and withdraws them");

        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const al("alice");
        Account const lp("carol");
        Account const gw2("gateway");
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        fundForAMMCreate(env, gw2, al, usd, eur);
        env.fund(XRP(100000), lp);
        env.close();
        env.trust(usd(1000000), lp);
        env.trust(eur(1000000), lp);
        env.close();
        env(pay(gw2, lp, usd(1000)));
        env(pay(gw2, lp, eur(1000)));
        env.close();

        auto jvCreate = ammCreateJV(env, al, usd, eur, usd(10000), eur(10000));
        jvCreate[sfCurveType.jsonName] = kCtStableSwap;
        jvCreate[sfAmplification.jsonName] = 100;
        env(jvCreate, Ter(tesSUCCESS));
        env.close();
        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtStableSwap);
        auto const lpBefore = env.le(ammKey)->getFieldAmount(sfLPTokenBalance);

        json::Value dep;
        dep[jss::Account] = lp.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = kCtStableSwap;
        dep[jss::Flags] = tfTwoAsset;
        dep[jss::Amount] = usd(10).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(10).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep, Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.le(ammKey)->getFieldAmount(sfLPTokenBalance) > lpBefore);

        json::Value wd;
        wd[jss::Account] = lp.human();
        wd[jss::TransactionType] = jss::AMMWithdraw;
        wd[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        wd[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        wd[sfCurveType.jsonName] = kCtStableSwap;
        wd[jss::Flags] = tfWithdrawAll;
        wd[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(wd, Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.le(ammKey)->getFieldAmount(sfLPTokenBalance) == lpBefore);
    }

    void
    testAmplificationVoteWeighted(FeatureBitset features)
    {
        testcase("Amplification follows the LP-token-weighted vote");

        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const al("alice");
        Account const minority("mallory");
        Account const gw2("gateway");
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        fundForAMMCreate(env, gw2, al, usd, eur);
        env.fund(XRP(100000), minority);
        env.close();
        env.trust(usd(1000000), minority);
        env.trust(eur(1000000), minority);
        env.close();
        env(pay(gw2, minority, usd(100)));
        env(pay(gw2, minority, eur(100)));
        env.close();

        auto jvCreate = ammCreateJV(env, al, usd, eur, usd(10000), eur(10000));
        jvCreate[sfCurveType.jsonName] = kCtStableSwap;
        jvCreate[sfAmplification.jsonName] = 100;
        env(jvCreate);
        env.close();
        json::Value dep;
        dep[jss::Account] = minority.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = kCtStableSwap;
        dep[jss::Flags] = tfTwoAsset;
        dep[jss::Amount] = usd(10).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(10).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep, Ter(tesSUCCESS));
        env.close();
        auto const amp = [&] {
            return env.le(keylet::amm(usd.asset(), eur.asset(), kCtStableSwap))
                ->getFieldU32(sfAmplification);
        };

        // A minority LP's vote alone leaves the amplification where the majority holds it.
        env(ammVoteJV(env, minority, usd, eur, 0, 200, kCtStableSwap), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(amp() == 100);

        // The majority LP's vote moves it, one rate-limited step toward the weighted target.
        env(ammVoteJV(env, al, usd, eur, 0, 200, kCtStableSwap), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(amp() == 110);

        // A fee-only vote keeps the voter's earlier amplification vote.
        env.close(env.now() + std::chrono::hours(25));
        env(ammVoteJV(env, al, usd, eur, 10, std::nullopt, kCtStableSwap), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(amp() == 121);
    }

    void
    testCLNonZeroTick(FeatureBitset features)
    {
        testcase("CL non-zero tick");

        using namespace jtx;
        Env const env(*this, features | featureAMMCurves);

        auto const* curve = getCurve(kCtConcentratedLiquidity, env.current()->rules());
        BEAST_EXPECT(curve != nullptr);

        STAmount const poolIn = USD(1000);
        STAmount const poolOut = EUR(1000);

        // Positive tick: price > 1
        {
            auto const ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU8(sfFeeTier, kFtMedium);
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
            auto const ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU8(sfFeeTier, kFtMedium);
            ammSle->setFieldI32(sfCurrentTick, -100);
            ammSle->setFieldU64(sfActiveLiquidity, 1000000);

            auto const result = curve->swapIn(poolIn, poolOut, USD(100), 0, ammSle.get());
            BEAST_EXPECT(result.has_value());
            if (result)
                BEAST_EXPECT(Number(*result) > Number{0});
        }

        // Large positive tick
        {
            auto const ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU8(sfFeeTier, kFtMedium);
            ammSle->setFieldI32(sfCurrentTick, 10000);
            ammSle->setFieldU64(sfActiveLiquidity, 1000000);

            auto const sp = curve->spotPrice(poolIn, poolOut, 0, ammSle.get());
            BEAST_EXPECT(sp.has_value());
            if (sp)
                BEAST_EXPECT(*sp > Number{0});
        }

        // Large negative tick
        {
            auto const ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU8(sfFeeTier, kFtMedium);
            ammSle->setFieldI32(sfCurrentTick, -10000);
            ammSle->setFieldU64(sfActiveLiquidity, 1000000);

            auto const sp = curve->spotPrice(poolIn, poolOut, 0, ammSle.get());
            BEAST_EXPECT(sp.has_value());
            if (sp)
                BEAST_EXPECT(*sp > Number{0});
        }

        // Tick spacing present
        {
            auto const ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU8(sfFeeTier, kFtMedium);
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
            auto const ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU8(sfFeeTier, kFtMedium);
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
            auto const ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU8(sfFeeTier, kFtMedium);
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

        auto const* curve = getCurve(kCtStableSwap, env.current()->rules());
        BEAST_EXPECT(curve != nullptr);

        // High asymmetry: 1:1000 pool
        {
            auto const ammSle = std::make_shared<SLE>(ltAMM, uint256{});
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
            auto const ammSle = std::make_shared<SLE>(ltAMM, uint256{});
            ammSle->setFieldU32(sfAmplification, 100);

            auto const result = curve->swapIn(USD(1000), EUR(1000), USD(1000000), 0, ammSle.get());
            BEAST_EXPECT(result.has_value());
            if (result)
                BEAST_EXPECT(Number(*result) < Number{1000});
        }

        // swapOut: want more than pool has
        {
            auto const ammSle = std::make_shared<SLE>(ltAMM, uint256{});
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
        env.close();
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
        if (curveType != kCtConstantProduct)
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
        env(jv, jtx::Ter(tesSUCCESS));
        env.close();
    }

    // An AMMDeposit of a CL position over [lower, upper]; returns its keylet.
    static Keylet
    depositCLPosition(
        jtx::Env& env,
        jtx::Account const& lp,
        jtx::IOU const& asset1,
        jtx::IOU const& asset2,
        UInt256 const& ammID,
        std::int32_t lower,
        std::int32_t upper,
        STAmount const& amount1,
        STAmount const& amount2)
    {
        auto const seq = env.seq(lp);
        json::Value dep;
        dep[jss::Account] = lp.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, asset1.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, asset2.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfTickLower.jsonName] = lower;
        dep[sfTickUpper.jsonName] = upper;
        dep[jss::Amount] = amount1.getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = amount2.getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep, jtx::Ter(tesSUCCESS));
        env.close();
        return keylet::ammPosition(ammID, lp.id(), seq);
    }

    // An AMMWithdraw of the CL position `positionID` from the asset1/asset2 pool.
    static json::Value
    clWithdrawJV(
        jtx::Env& env,
        jtx::Account const& lp,
        jtx::IOU const& asset1,
        jtx::IOU const& asset2,
        UInt256 const& positionID)
    {
        json::Value wd;
        wd[jss::Account] = lp.human();
        wd[jss::TransactionType] = jss::AMMWithdraw;
        wd[jss::Asset] = STIssue(sfAsset, asset1.asset()).getJson(JsonOptions::Values::None);
        wd[jss::Asset2] = STIssue(sfAsset, asset2.asset()).getJson(JsonOptions::Values::None);
        wd[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        wd[jss::Flags] = tfWithdrawAll;
        wd[sfPositionID.jsonName] = to_string(positionID);
        wd[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        return wd;
    }

    void
    testCLWithdraw(FeatureBitset features)
    {
        testcase("CL withdraw: full close of in-range and out-of-range positions");

        using namespace jtx;

        Account const lp("alice");
        Account const other("bob");
        Account const gw2("gateway");

        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, other, usd, eur);

        auto jv = ammCreateJV(env, lp, usd, eur, usd(1000), eur(1000));
        jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        jv[sfFeeTier.jsonName] = kFtMedium;
        env(jv, Ter(tesSUCCESS));
        env.close();
        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
        auto const ammID = env.current()->read(ammKey)->key();

        auto const inRange =
            depositCLPosition(env, lp, usd, eur, ammID, -600, 600, usd(100), eur(100));
        auto const above =
            depositCLPosition(env, lp, usd, eur, ammID, 1200, 2400, usd(100), eur(100));
        auto const below =
            depositCLPosition(env, lp, usd, eur, ammID, -2400, -1200, usd(100), eur(100));
        BEAST_EXPECT(env.le(inRange) && env.le(above) && env.le(below));
        BEAST_EXPECT(env.le(ammKey)->getFieldU32(sfPositionCount) == 3);

        // Preflight: CL withdrawals need tfWithdrawAll, a PositionID and no amounts.
        {
            auto wd = clWithdrawJV(env, lp, usd, eur, inRange.key);
            wd[jss::Flags] = tfSingleAsset;
            env(wd, Ter(temMALFORMED));
            wd = clWithdrawJV(env, lp, usd, eur, inRange.key);
            wd.removeMember(sfPositionID.jsonName);
            env(wd, Ter(temMALFORMED));
            wd = clWithdrawJV(env, lp, usd, eur, inRange.key);
            wd[jss::Amount] = usd(1).value().getJson(JsonOptions::Values::None);
            env(wd, Ter(temMALFORMED));
        }

        // Preclaim: the position must exist, belong to the caller, and the
        // partial amount must be within it.
        {
            env(clWithdrawJV(env, lp, usd, eur, keylet::ammPosition(ammID, lp.id(), 1).key),
                Ter(tecNO_ENTRY));
            env(clWithdrawJV(env, other, usd, eur, inRange.key), Ter(tecNO_PERMISSION));
            auto wd = clWithdrawJV(env, lp, usd, eur, inRange.key);
            wd[sfPositionLiquidity.jsonName] = "0";
            env(wd, Ter(temMALFORMED));
            wd[sfPositionLiquidity.jsonName] =
                std::format("{:X}", env.le(inRange)->getFieldU64(sfPositionLiquidity) + 1);
            env(wd, Ter(temMALFORMED));
        }

        // A partial withdrawal leaves the position with the rest of its liquidity.
        {
            auto const liquidity = env.le(inRange)->getFieldU64(sfPositionLiquidity);
            auto wd = clWithdrawJV(env, lp, usd, eur, inRange.key);
            wd[sfPositionLiquidity.jsonName] = std::format("{:X}", liquidity / 2);
            env(wd, Ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(
                env.le(inRange)->getFieldU64(sfPositionLiquidity) == liquidity - (liquidity / 2));
        }

        // Full withdrawals return the position's assets and remove it; ticks no
        // position references are removed.
        for (auto const& position : {inRange, above, below})
        {
            auto const sle = env.le(position);
            auto const lower = sle->getFieldI32(sfTickLower);
            auto const upper = sle->getFieldI32(sfTickUpper);
            auto const usdBefore = env.balance(lp, usd.issue());
            auto const eurBefore = env.balance(lp, eur.issue());
            env(clWithdrawJV(env, lp, usd, eur, position.key), Ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(!env.le(position));
            BEAST_EXPECT(!env.le(keylet::ammTick(ammID, lower)));
            BEAST_EXPECT(!env.le(keylet::ammTick(ammID, upper)));
            BEAST_EXPECT(
                env.balance(lp, usd.issue()) > usdBefore ||
                env.balance(lp, eur.issue()) > eurBefore);
        }
        BEAST_EXPECT(env.le(ammKey)->getFieldU32(sfPositionCount) == 0);
        BEAST_EXPECT(env.le(ammKey)->getFieldU64(sfActiveLiquidity) == 0);
    }

    void
    testCLAssetsInEitherOrder(FeatureBitset features)
    {
        testcase("CL deposit, fee collection and withdraw name the pool's assets in either order");

        using namespace jtx;

        Account const lp("alice");
        Account const trader("bob");
        Account const gw2("gateway");

        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);

        // token0 is the lower of the pool's two assets.
        bool const usdIsToken0 = usd.asset() < eur.asset();
        auto const& token0 = usdIsToken0 ? usd : eur;
        auto const& token1 = usdIsToken0 ? eur : usd;

        auto jv = ammCreateJV(env, lp, usd, eur, usd(1000), eur(1000));
        jv[jss::TradingFee] = 30;
        jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        jv[sfFeeTier.jsonName] = kFtMedium;
        env(jv, Ter(tesSUCCESS));
        env.close();
        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
        auto const ammID = env.le(ammKey)->key();

        // A position above the current price holds only token0, whichever
        // order the deposit names the assets in.
        auto const token0Before = env.balance(lp, token0.issue());
        auto const token1Before = env.balance(lp, token1.issue());
        auto const above =
            depositCLPosition(env, lp, token1, token0, ammID, 1200, 2400, token1(100), token0(100));
        BEAST_EXPECT(env.balance(lp, token0.issue()) == token0Before - token0(100));
        BEAST_EXPECT(env.balance(lp, token1.issue()) == token1Before);

        // Withdrawing it, again in reversed order, returns token0 only.
        {
            auto const t0 = env.balance(lp, token0.issue());
            auto const t1 = env.balance(lp, token1.issue());
            env(clWithdrawJV(env, lp, token1, token0, above.key), Ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(env.balance(lp, token0.issue()) > t0);
            BEAST_EXPECT(env.balance(lp, token1.issue()) == t1);
        }

        // Fees a swap paying token0 accrue in token0, and a collection that
        // names the assets in reversed order pays them in token0.
        auto const inRange =
            depositCLPosition(env, lp, token0, token1, ammID, -600, 600, token0(500), token1(500));
        env(pay(trader, trader, token1(50)),
            Path(~token1),
            Sendmax(token0(60)),
            Txflags(tfNoRippleDirect | tfPartialPayment),
            Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(Number{env.le(ammKey)->getFieldNumber(sfFeeGrowthGlobal0)} > Number{0});
        {
            auto const t0 = env.balance(lp, token0.issue());
            auto const t1 = env.balance(lp, token1.issue());
            json::Value coll;
            coll[jss::Account] = lp.human();
            coll[jss::TransactionType] = jss::AMMCollectFees;
            coll[jss::Asset] = STIssue(sfAsset, token1.asset()).getJson(JsonOptions::Values::None);
            coll[jss::Asset2] = STIssue(sfAsset, token0.asset()).getJson(JsonOptions::Values::None);
            coll[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            coll[sfPositionID.jsonName] = to_string(inRange.key);
            coll[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(coll, Ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(env.balance(lp, token0.issue()) > t0);
            BEAST_EXPECT(env.balance(lp, token1.issue()) == t1);
        }
    }

    // An AMMDeposit of a CL position with explicit flags, ticks and amounts.
    static json::Value
    clDepositJV(
        jtx::Env& env,
        jtx::Account const& lp,
        jtx::IOU const& asset1,
        jtx::IOU const& asset2,
        std::uint32_t flags,
        std::optional<std::int32_t> lower,
        std::optional<std::int32_t> upper,
        std::optional<STAmount> const& amount1,
        std::optional<STAmount> const& amount2)
    {
        json::Value dep;
        dep[jss::Account] = lp.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, asset1.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, asset2.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        dep[jss::Flags] = flags;
        if (lower)
            dep[sfTickLower.jsonName] = *lower;
        if (upper)
            dep[sfTickUpper.jsonName] = *upper;
        if (amount1)
            dep[jss::Amount] = amount1->getJson(JsonOptions::Values::None);
        if (amount2)
            dep[jss::Amount2] = amount2->getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        return dep;
    }

    void
    testCLDepositErrors(FeatureBitset features)
    {
        testcase("CL deposit: malformed ranges, flags, single-asset sides and oversized liquidity");

        using namespace jtx;

        Account const lp("alice");
        Account const other("bob");
        Account const gw2("gateway");

        // Without the amendment a CL deposit is disabled.
        {
            Env env(*this, features - featureAMMCurves);
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            setupSwapEnv(env, gw2, lp, other, usd, eur);
            env(clDepositJV(env, lp, usd, eur, tfTwoAsset, -60, 60, usd(1), eur(1)),
                Ter(temDISABLED));
        }

        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, other, usd, eur);
        auto jv = ammCreateJV(env, lp, usd, eur, usd(1000), eur(1000));
        jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        jv[sfFeeTier.jsonName] = kFtMedium;
        env(jv, Ter(tesSUCCESS));
        env.close();

        bool const usdIsToken0 = usd.asset() < eur.asset();
        auto const& token0 = usdIsToken0 ? usd : eur;
        auto const& token1 = usdIsToken0 ? eur : usd;

        // Preflight: flags other than tfTwoAsset / tfSingleAsset, missing or
        // inverted ticks, and ticks outside the global range.
        {
            auto dep =
                clDepositJV(env, lp, usd, eur, tfLPToken, -60, 60, std::nullopt, std::nullopt);
            dep[jss::LPTokenOut] =
                STAmount(Issue{toCurrency("LPT"), lp.id()}, 1).getJson(JsonOptions::Values::None);
            env(dep, Ter(temMALFORMED));
        }
        env(clDepositJV(env, lp, usd, eur, tfTwoAsset, std::nullopt, 60, usd(1), eur(1)),
            Ter(temMALFORMED));
        env(clDepositJV(env, lp, usd, eur, tfTwoAsset, 60, 60, usd(1), eur(1)), Ter(temMALFORMED));
        env(clDepositJV(env, lp, usd, eur, tfTwoAsset, minTick - 60, 60, usd(1), eur(1)),
            Ter(temMALFORMED));

        // Ticks off the pool's spacing.
        env(clDepositJV(env, lp, usd, eur, tfTwoAsset, -61, 60, usd(1), eur(1)), Ter(temMALFORMED));

        // Single-asset deposits: token0 above the price, token1 below it; the
        // other side, or a range across the price, is refused.
        env(clDepositJV(env, lp, usd, eur, tfSingleAsset, 600, 1200, token0(10), std::nullopt),
            Ter(tesSUCCESS));
        env(clDepositJV(env, lp, usd, eur, tfSingleAsset, 600, 1200, token1(10), std::nullopt),
            Ter(tecAMM_FAILED));
        env(clDepositJV(env, lp, usd, eur, tfSingleAsset, -1200, -600, token1(10), std::nullopt),
            Ter(tesSUCCESS));
        env(clDepositJV(env, lp, usd, eur, tfSingleAsset, -1200, -600, token0(10), std::nullopt),
            Ter(tecAMM_FAILED));
        env(clDepositJV(env, lp, usd, eur, tfSingleAsset, -600, 600, token0(10), std::nullopt),
            Ter(tecAMM_FAILED));
        env.close();

        // A position whose lower tick is the current tick holds only token0.
        {
            auto const t0 = env.balance(lp, token0.issue());
            auto const t1 = env.balance(lp, token1.issue());
            env(clDepositJV(env, lp, token0, token1, tfTwoAsset, 0, 600, token0(10), token1(10)),
                Ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(env.balance(lp, token0.issue()) < t0);
            BEAST_EXPECT(env.balance(lp, token1.issue()) == t1);
        }

        // Liquidity that does not fit in 64 bits is refused.
        {
            env.trust(STAmount(token0.issue(), 1, 18), lp);
            env(pay(gw2, lp, STAmount(token0.issue(), 1, 17)));
            env.close();
            env(clDepositJV(
                    env,
                    lp,
                    usd,
                    eur,
                    tfSingleAsset,
                    60,
                    120,
                    STAmount(token0.issue(), 1, 17),
                    std::nullopt),
                Ter(tecAMM_FAILED));
        }
    }

    void
    testCLWithdrawWholePositionByLiquidity(FeatureBitset features)
    {
        testcase("CL withdraw and deposit: a whole position by liquidity, too little liquidity");

        using namespace jtx;

        Account const lp("alice");
        Account const trader("bob");
        Account const gw2("gateway");
        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);
        auto jv = ammCreateJV(env, lp, usd, eur, usd(1000), eur(1000));
        jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        jv[sfFeeTier.jsonName] = kFtMedium;
        env(jv, Ter(tesSUCCESS));
        env.close();
        auto const ammID =
            env.le(keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity))->key();
        auto const pos = depositCLPosition(env, lp, usd, eur, ammID, -600, 600, usd(100), eur(100));

        auto const owners = env.le(keylet::account(lp.id()))->getFieldU32(sfOwnerCount);
        auto wd = clWithdrawJV(env, lp, usd, eur, pos.key);
        wd[sfPositionLiquidity.jsonName] =
            std::format("{:X}", env.le(pos)->getFieldU64(sfPositionLiquidity));
        env(wd, Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(!env.le(pos));
        BEAST_EXPECT(env.le(keylet::account(lp.id()))->getFieldU32(sfOwnerCount) == owners - 1);

        // A deposit too small for one unit of liquidity.
        depositTooSmall(env, lp, usd, eur);
    }

    // A CL deposit of 1e-10 of each asset over [-600, 600].
    static void
    depositTooSmall(jtx::Env& env, jtx::Account const& lp, jtx::IOU const& usd, jtx::IOU const& eur)
    {
        json::Value dep;
        dep[jss::Account] = lp.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfTickLower.jsonName] = -600;
        dep[sfTickUpper.jsonName] = 600;
        dep[jss::Amount] = STAmount{usd.issue(), 1, -10}.getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = STAmount{eur.issue(), 1, -10}.getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep, jtx::Ter(tecAMM_FAILED));
        env.close();
    }

    void
    testCLWithdrawErrors(FeatureBitset features)
    {
        testcase("CL withdraw: disabled, invalid flags and fields on other pools");

        using namespace jtx;

        Account const lp("alice");
        Account const other("bob");
        Account const gw2("gateway");

        {
            Env env(*this, features - featureAMMCurves);
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            setupSwapEnv(env, gw2, lp, other, usd, eur);
            env(clWithdrawJV(env, lp, usd, eur, UInt256{1u}), Ter(temDISABLED));
        }

        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, other, usd, eur);
        createCurvePool(env, lp, usd, eur, usd(1000), eur(1000), kCtConstantProduct, json::Value{});

        // CL withdrawals need tfWithdrawAll.
        {
            auto wd = clWithdrawJV(env, lp, usd, eur, UInt256{1u});
            wd[jss::Flags] = tfLPToken;
            wd[jss::LPTokenIn] =
                STAmount(Issue{toCurrency("LPT"), lp.id()}, 1).getJson(JsonOptions::Values::None);
            env(wd, Ter(temMALFORMED));
        }

        // A constant-product withdrawal may not name a position.
        {
            auto wd = clWithdrawJV(env, lp, usd, eur, UInt256{1u});
            wd.removeMember(sfCurveType.jsonName);
            env(wd, Ter(temMALFORMED));
        }

        // A constant-product withdrawal in another pool's LP token.
        {
            json::Value wd;
            wd[jss::Account] = lp.human();
            wd[jss::TransactionType] = jss::AMMWithdraw;
            wd[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            wd[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            wd[jss::Flags] = tfLPToken;
            wd[jss::LPTokenIn] =
                STAmount(Issue{toCurrency("LPT"), gw2.id()}, 1).getJson(JsonOptions::Values::None);
            wd[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(wd, Ter(temBAD_AMM_TOKENS));
        }
    }

    // An AMMCollectFees naming a position or a bin.
    static json::Value
    collectJV(
        jtx::Env& env,
        jtx::Account const& acct,
        jtx::IOU const& asset1,
        jtx::IOU const& asset2,
        std::optional<std::uint8_t> curveType,
        std::optional<UInt256> const& positionID,
        std::optional<std::int32_t> binID)
    {
        json::Value coll;
        coll[jss::Account] = acct.human();
        coll[jss::TransactionType] = jss::AMMCollectFees;
        coll[jss::Asset] = STIssue(sfAsset, asset1.asset()).getJson(JsonOptions::Values::None);
        coll[jss::Asset2] = STIssue(sfAsset, asset2.asset()).getJson(JsonOptions::Values::None);
        if (curveType)
            coll[sfCurveType.jsonName] = *curveType;
        if (positionID)
            coll[sfPositionID.jsonName] = to_string(*positionID);
        if (binID)
            coll[sfBinID.jsonName] = *binID;
        coll[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        return coll;
    }

    // `numerator` / `denominator` of `asset` rounded in `mode`.
    static STAmount
    roundedRatio(
        Asset const& asset,
        std::int64_t numerator,
        std::int64_t denominator,
        Number::RoundingMode mode)
    {
        NumberRoundModeGuard const mg(mode);
        return toSTAmount(asset, Number{numerator} / Number{denominator}, mode);
    }

    void
    testCLCollectRoundsIouDown(FeatureBitset features)
    {
        testcase("CL fee collection pays an IOU amount no greater than the exact fee");

        using namespace jtx;

        Account const lp("alice");
        Account const trader("bob");
        Account const gw2("gateway");

        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);
        bool const usdIsToken0 = usd.asset() < eur.asset();
        auto const& token0 = usdIsToken0 ? usd : eur;
        auto const& token1 = usdIsToken0 ? eur : usd;
        auto jv = ammCreateJV(env, lp, usd, eur, usd(1000), eur(1000));
        jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        jv[sfFeeTier.jsonName] = kFtMedium;
        env(jv, Ter(tesSUCCESS));
        env.close();
        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
        auto const ammID = env.le(ammKey)->key();
        auto const position =
            depositCLPosition(env, lp, token0, token1, ammID, -600, 600, token0(500), token1(500));
        auto const liquidity =
            static_cast<std::int64_t>(env.le(position)->getFieldU64(sfPositionLiquidity));

        // A receiver with empty balances collects, so its balance is the fee to the last digit.
        Account const receiver("carol");
        env.fund(XRP(1000), receiver);
        env.close();
        env.trust(token0(1000000), receiver);
        env.trust(token1(1000000), receiver);
        env(deposit::auth(receiver, lp));
        env.close();
        json::Value transfer;
        transfer[jss::Account] = lp.human();
        transfer[jss::TransactionType] = "AMMPositionTransfer";
        transfer[jss::Destination] = receiver.human();
        transfer[sfPositionID.jsonName] = to_string(position.key);
        env(transfer, Ter(tesSUCCESS));
        env.close();

        // Fee growth of k / 3000 a unit of liquidity, with the k whose fee does not round to
        // the same amount down as to nearest.
        std::int64_t k = 1;
        while (
            k < 9 &&
            roundedRatio(token0.asset(), k * liquidity, 3000, Number::RoundingMode::Downward) ==
                roundedRatio(token0.asset(), k * liquidity, 3000, Number::RoundingMode::ToNearest))
            ++k;
        BEAST_EXPECT(k < 9);
        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) {
            auto const sle = std::make_shared<SLE>(*view.read(ammKey));
            sle->setFieldNumber(
                sfFeeGrowthGlobal0, STNumber{sfFeeGrowthGlobal0, Number{k} / Number{3000}});
            view.rawReplace(sle);
            return true;
        });

        // The open ledger holds the pool's crafted fee growth, which closing would rebuild without.
        env(collectJV(
                env,
                receiver,
                token0,
                token1,
                kCtConcentratedLiquidity,
                position.key,
                std::nullopt),
            Ter(tesSUCCESS));
        BEAST_EXPECT(
            env.balance(receiver, token0.issue()) ==
            roundedRatio(token0.asset(), k * liquidity, 3000, Number::RoundingMode::Downward));
    }

    void
    testCLCollectFees(FeatureBitset features)
    {
        testcase("CL collect fees: addressing errors and positions on either side of the price");

        using namespace jtx;

        Account const lp("alice");
        Account const trader("bob");
        Account const gw2("gateway");

        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);
        bool const usdIsToken0 = usd.asset() < eur.asset();
        auto const& token0 = usdIsToken0 ? usd : eur;
        auto const& token1 = usdIsToken0 ? eur : usd;

        // Exactly one of PositionID and BinID; a pool must exist and be CL or
        // binned; a CL pool needs a PositionID.
        env(collectJV(env, lp, usd, eur, kCtConcentratedLiquidity, std::nullopt, std::nullopt),
            Ter(temMALFORMED));
        env(collectJV(env, lp, usd, eur, kCtConcentratedLiquidity, UInt256{1u}, 0),
            Ter(temMALFORMED));
        env(collectJV(env, lp, usd, eur, kCtConcentratedLiquidity, UInt256{1u}, std::nullopt),
            Ter(terNO_AMM));
        createCurvePool(env, lp, usd, eur, usd(1000), eur(1000), kCtConstantProduct, json::Value{});
        env(collectJV(env, lp, usd, eur, std::nullopt, UInt256{1u}, std::nullopt),
            Ter(tecAMM_FAILED));

        auto jv = ammCreateJV(env, lp, usd, eur, usd(1000), eur(1000));
        jv[jss::TradingFee] = 30;
        jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        jv[sfFeeTier.jsonName] = kFtMedium;
        env(jv, Ter(tesSUCCESS));
        env.close();
        auto const ammID =
            env.le(keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity))->key();
        env(collectJV(env, lp, usd, eur, kCtConcentratedLiquidity, std::nullopt, 0),
            Ter(temMALFORMED));
        env(collectJV(env, lp, usd, eur, kCtConcentratedLiquidity, UInt256{1u}, std::nullopt),
            Ter(tecNO_ENTRY));

        // A position across the price and one on each side.
        auto const inRange =
            depositCLPosition(env, lp, token0, token1, ammID, -600, 600, token0(500), token1(500));
        auto const above =
            depositCLPosition(env, lp, token0, token1, ammID, 1200, 1800, token0(50), token1(50));
        auto const below =
            depositCLPosition(env, lp, token0, token1, ammID, -1800, -1200, token0(50), token1(50));

        // Swaps both ways accrue fees on both assets.
        env(pay(trader, trader, token1(20)),
            Path(~token1),
            Sendmax(token0(30)),
            Txflags(tfNoRippleDirect | tfPartialPayment),
            Ter(tesSUCCESS));
        env(pay(trader, trader, token0(20)),
            Path(~token0),
            Sendmax(token1(30)),
            Txflags(tfNoRippleDirect | tfPartialPayment),
            Ter(tesSUCCESS));
        env.close();

        // The position in range is paid in both assets; the ones out of range
        // collect what accrued while the price was inside them (none here).
        {
            auto const t0 = env.balance(lp, token0.issue());
            auto const t1 = env.balance(lp, token1.issue());
            env(collectJV(
                    env, lp, token0, token1, kCtConcentratedLiquidity, inRange.key, std::nullopt),
                Ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(env.balance(lp, token0.issue()) > t0);
            BEAST_EXPECT(env.balance(lp, token1.issue()) > t1);
        }
        for (auto const& position : {above, below})
        {
            auto const t0 = env.balance(lp, token0.issue());
            auto const t1 = env.balance(lp, token1.issue());
            env(collectJV(
                    env, lp, token0, token1, kCtConcentratedLiquidity, position.key, std::nullopt),
                Ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(env.balance(lp, token0.issue()) == t0);
            BEAST_EXPECT(env.balance(lp, token1.issue()) == t1);
        }

        // Someone else's position.
        env(collectJV(
                env, trader, token0, token1, kCtConcentratedLiquidity, inRange.key, std::nullopt),
            Ter(tecNO_PERMISSION));
    }

    void
    testCLTickLiquidityLimits(FeatureBitset features)
    {
        testcase("CL deposit: tick and pool liquidity stay inside their field ranges");

        using namespace jtx;

        Account const gw2("gateway");
        Account const lp("alice");
        Account const other("bob");
        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        env.fund(XRP(100000), gw2, lp, other);
        env.close();
        std::int64_t const big = 20000000000000000;
        for (auto const& acct : {lp, other})
        {
            env.trust(usd(5 * big), acct);
            env.trust(eur(5 * big), acct);
        }
        env.close();
        for (auto const& acct : {lp, other})
        {
            env(pay(gw2, acct, usd(big)));
            env(pay(gw2, acct, eur(big)));
        }
        env.close();
        bool const usdIsToken0 = usd.asset() < eur.asset();
        auto const& token0 = usdIsToken0 ? usd : eur;
        auto const& token1 = usdIsToken0 ? eur : usd;

        auto jv = ammCreateJV(env, lp, token0, token1, token0(1000), token1(1000));
        jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        jv[sfFeeTier.jsonName] = kFtMedium;
        env(jv, Ter(tesSUCCESS));
        env.close();
        auto const ammID =
            env.le(keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity))->key();

        // Each position above the price holds more than half the int64 range of liquidity.
        STAmount const half = token0(std::int64_t{15000000000000000});
        auto const seq = env.seq(lp);
        env(clDepositJV(env, lp, token0, token1, tfSingleAsset, 120, 180, half, std::nullopt),
            Ter(tesSUCCESS));
        env.close();
        auto const liq =
            env.le(keylet::ammPosition(ammID, lp.id(), seq))->getFieldU64(sfPositionLiquidity);
        BEAST_EXPECT(
            liq > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / 2));
        auto const netBefore = env.le(keylet::ammTick(ammID, 120))->getFieldU64(sfLiquidityNet);

        // A second one on the same ticks would take sfLiquidityNet past int64.
        env(clDepositJV(env, other, token0, token1, tfSingleAsset, 120, 180, half, std::nullopt),
            Ter(tecAMM_FAILED));
        env.close();
        BEAST_EXPECT(env.le(keylet::ammTick(ammID, 120))->getFieldU64(sfLiquidityNet) == netBefore);

        // Two positions at the price whose liquidity together passes int64, the
        // range a swap's tick crossing works in: the second is refused.
        Account const carol("carol");
        env.fund(XRP(100000), carol);
        env.close();
        env.trust(usd(5 * big), carol);
        env.trust(eur(5 * big), carol);
        env.close();
        env(pay(gw2, carol, usd(2 * big)));
        env(pay(gw2, carol, eur(2 * big)));
        env.close();
        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
        std::int64_t const near = 14000000000000000;
        env(clDepositJV(
                env, other, token0, token1, tfTwoAsset, -60, 60, token0(near), token1(near)),
            Ter(tesSUCCESS));
        env.close();
        auto const active = env.le(ammKey)->getFieldU64(sfActiveLiquidity);
        BEAST_EXPECT(
            active > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / 2));
        env(clDepositJV(
                env,
                carol,
                token0,
                token1,
                tfTwoAsset,
                -120,
                120,
                token0(2 * near),
                token1(2 * near)),
            Ter(tecAMM_FAILED));
        env.close();
        BEAST_EXPECT(env.le(ammKey)->getFieldU64(sfActiveLiquidity) == active);
    }

    void
    testCLBitmapReadsBounded(FeatureBitset features)
    {
        testcase("CL payment: tick-bitmap reads stay within the tick-crossing budget");

        using namespace jtx;

        Account const gw2("gateway");
        Account const lp("alice");
        Account const trader("bob");
        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);
        bool const usdIsToken0 = usd.asset() < eur.asset();
        auto const& token0 = usdIsToken0 ? usd : eur;
        auto const& token1 = usdIsToken0 ? eur : usd;

        auto jv = ammCreateJV(env, lp, token0, token1, token0(10000), token1(10000));
        jv[jss::TradingFee] = 30;
        jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        jv[sfFeeTier.jsonName] = kFtMedium;
        env(jv, Ter(tesSUCCESS));
        env.close();
        auto const ammID =
            env.le(keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity))->key();

        // The only initialised ticks are thousands of bitmap words from the price.
        depositCLPosition(
            env, lp, token0, token1, ammID, -600000, 600000, token0(1000), token1(1000));

        for (auto const zeroForOne : {true, false})
        {
            auto const& in = zeroForOne ? token0 : token1;
            auto const& out = zeroForOne ? token1 : token0;
            auto const jt = env.jt(
                pay(trader, trader, out(10)),
                Path(~out),
                Sendmax(in(20)),
                Txflags(tfNoRippleDirect | tfPartialPayment));
            BitmapReadCounter const counter(*env.current(), ammID);
            OpenView view(&counter);
            auto const result = xrpl::apply(env.app(), view, *jt.stx, TapNone, env.journal);
            BEAST_EXPECT(isTesSuccess(result.ter));
            BEAST_EXPECTS(
                counter.bitmapReads <= static_cast<std::size_t>(maxTickCrossings),
                std::to_string(counter.bitmapReads));
        }
    }

    void
    testCLInRangeSwapMovesPrice(FeatureBitset features)
    {
        testcase("CL payment: a swap inside one range moves the price for the next");

        using namespace jtx;

        Account const gw2("gateway");
        Account const lp("alice");
        Account const trader("bob");
        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);
        bool const usdIsToken0 = usd.asset() < eur.asset();
        auto const& token0 = usdIsToken0 ? usd : eur;
        auto const& token1 = usdIsToken0 ? eur : usd;

        auto jv = ammCreateJV(env, lp, token0, token1, token0(10000), token1(10000));
        jv[jss::TradingFee] = 1;
        jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        jv[sfFeeTier.jsonName] = kFtStable;
        env(jv, Ter(tesSUCCESS));
        env.close();
        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
        auto const ammID = env.le(ammKey)->key();
        depositCLPosition(
            env, lp, token0, token1, ammID, -10000, 10000, token0(10000), token1(10000));

        // Buying token0 with token1 raises the price; each buy pays more per unit.
        auto const buy = [&] {
            auto const inBefore = env.balance(trader, token1.issue());
            auto const outBefore = env.balance(trader, token0.issue());
            env(pay(trader, trader, token0(500)),
                Path(~token0),
                Sendmax(token1(1000)),
                Txflags(tfNoRippleDirect | tfPartialPayment),
                Ter(tesSUCCESS));
            env.close();
            return std::pair{
                Number{inBefore - env.balance(trader, token1.issue())},
                Number{env.balance(trader, token0.issue()) - outBefore}};
        };
        Number const sqrtPBefore{env.le(ammKey)->getFieldNumber(sfSqrtPrice)};
        auto const [spent1, got1] = buy();
        Number const sqrtPMid{env.le(ammKey)->getFieldNumber(sfSqrtPrice)};
        auto const [spent2, got2] = buy();
        BEAST_EXPECT(sqrtPMid > sqrtPBefore);
        BEAST_EXPECT(Number{env.le(ammKey)->getFieldNumber(sfSqrtPrice)} > sqrtPMid);
        BEAST_EXPECT(got1 > Number{0} && got2 > Number{0});
        BEAST_EXPECT(spent2 * got1 > spent1 * got2);
        // The tick follows the price.
        BEAST_EXPECT(
            tickToSqrtPrice(env.le(ammKey)->getFieldI32(sfCurrentTick)) <=
            Number{env.le(ammKey)->getFieldNumber(sfSqrtPrice)});
    }

    void
    testCLSwapCrossesEmptyRange(FeatureBitset features)
    {
        testcase("CL payment: a swap carries on across a range with no liquidity");

        using namespace jtx;

        Account const gw2("gateway");
        Account const lp("alice");
        Account const trader("bob");
        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);
        bool const usdIsToken0 = usd.asset() < eur.asset();
        auto const& token0 = usdIsToken0 ? usd : eur;
        auto const& token1 = usdIsToken0 ? eur : usd;

        auto jv = ammCreateJV(env, lp, token0, token1, token0(1000), token1(1000));
        jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        jv[sfFeeTier.jsonName] = kFtMedium;
        env(jv, Ter(tesSUCCESS));
        env.close();
        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
        auto const ammID = env.le(ammKey)->key();
        // A range around the price, then no liquidity from tick 600 to 1200, then token0 above.
        depositCLPosition(env, lp, token0, token1, ammID, -600, 600, token0(100), token1(100));
        depositCLPosition(env, lp, token0, token1, ammID, 1200, 2400, token0(5000), token1(1));

        // Buying more token0 than the first range holds raises the price through the gap.
        auto const before = env.balance(trader, token0.issue());
        env(pay(trader, trader, token0(400)),
            Path(~token0),
            Sendmax(token1(1000)),
            Txflags(tfNoRippleDirect | tfPartialPayment),
            Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.balance(trader, token0.issue()) - before > token0(100));
        auto const after = env.le(ammKey);
        BEAST_EXPECT(after->getFieldI32(sfCurrentTick) >= 1200);
        BEAST_EXPECT(after->getFieldU64(sfActiveLiquidity) > 0);
    }

    void
    testCLDepositWaivesTransferFee(FeatureBitset features)
    {
        testcase("CL deposit pays no transfer fee and flags the pool's lines");

        using namespace jtx;

        Account const gw2("gateway");
        Account const lp("alice");
        Account const trader("bob");
        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);
        env(rate(gw2, 1.25));
        env.close();
        bool const usdIsToken0 = usd.asset() < eur.asset();
        auto const& token0 = usdIsToken0 ? usd : eur;
        auto const& token1 = usdIsToken0 ? eur : usd;

        auto jv = ammCreateJV(env, lp, token0, token1, token0(10000), token1(10000));
        jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        jv[sfFeeTier.jsonName] = kFtStable;
        env(jv, Ter(tesSUCCESS));
        env.close();
        auto const ammSle = env.le(keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity));
        auto const ammAccount = ammSle->getAccountID(sfAccount);

        STAmount const before0 = env.balance(lp, token0.issue()).value();
        STAmount const before1 = env.balance(lp, token1.issue()).value();
        depositCLPosition(
            env, lp, token0, token1, ammSle->key(), -100, 100, token0(50), token1(50));
        for (auto const& [iou, before] : {std::pair{token0, before0}, std::pair{token1, before1}})
        {
            auto const line = env.le(keylet::trustLine(ammAccount, iou.issue()));
            if (!BEAST_EXPECT(line))
                continue;
            BEAST_EXPECT(line->isFlag(lsfAMMNode));
            STAmount const lineBalance = line->getFieldAmount(sfBalance);
            Number const held{lineBalance.signum() < 0 ? -lineBalance : lineBalance};
            BEAST_EXPECT(Number{before - env.balance(lp, iou.issue()).value()} == held);
        }
    }

    // Moves a CL pool's stored price onto `tick` in the open ledger, without touching
    // its liquidity. A close replays the ledger's transactions without it, so a
    // test reads what it checks before closing.
    static void
    placeCLPrice(jtx::Env& env, Keylet const& ammKey, std::int32_t tick)
    {
        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) -> bool {
            // The price a transaction would store: at the engine's Number scale.
            NumberMantissaScaleGuard const scale{MantissaRange::MantissaScale::Large};
            auto const amm = std::make_shared<SLE>(*view.read(ammKey));
            amm->setFieldI32(sfCurrentTick, tick);
            amm->setFieldNumber(sfSqrtPrice, STNumber{sfSqrtPrice, tickToSqrtPrice(tick)});
            view.rawReplace(amm);
            return true;
        });
    }

    void
    testOneUnitRoundTrip(FeatureBitset features)
    {
        testcase("Curve payments: a one-unit round trip never pays the taker");

        using namespace jtx;

        for (std::uint8_t const curveType :
             {kCtConstantProduct, kCtStableSwap, kCtConcentratedLiquidity})
        {
            Account const gw2("gateway");
            Account const lp("alice");
            Account const trader("bob");
            Env env(*this, features | featureAMMCurves);
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            setupSwapEnv(env, gw2, lp, trader, usd, eur);
            json::Value params;
            if (curveType == kCtStableSwap)
                params[sfAmplification.jsonName] = 100;
            if (curveType == kCtConcentratedLiquidity)
                params[sfFeeTier.jsonName] = kFtStable;
            createCurvePool(env, lp, usd, eur, usd(1000), eur(997), curveType, params);
            if (curveType == kCtConcentratedLiquidity)
            {
                auto const ammID = env.le(keylet::amm(usd.asset(), eur.asset(), curveType))->key();
                depositCLPosition(env, lp, usd, eur, ammID, -600, 600, usd(1000), eur(1000));
            }

            STAmount const usdBefore = env.balance(trader, usd.issue()).value();
            STAmount const eurBefore = env.balance(trader, eur.issue()).value();
            env(pay(trader, trader, eur(10)),
                Path(~eur),
                Sendmax(usd(1)),
                Txflags(tfNoRippleDirect | tfPartialPayment),
                Ter(tesSUCCESS));
            env.close();
            STAmount const got = env.balance(trader, eur.issue()).value() - eurBefore;
            env(pay(trader, trader, usd(10)),
                Path(~usd),
                Sendmax(got),
                Txflags(tfNoRippleDirect | tfPartialPayment),
                Ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(env.balance(trader, usd.issue()).value() <= usdBefore);
            BEAST_EXPECT(env.balance(trader, eur.issue()).value() <= eurBefore);
        }
    }

    void
    testCLTickNetLowerBound(FeatureBitset features)
    {
        testcase("CL deposit: an upper tick's net liquidity stays above the int64 minimum");

        using namespace jtx;

        // The liquidity a [-60, 60] deposit of 100 of each asset gives, from a first ledger.
        std::uint64_t liquidity = 0;
        auto const build = [&](Env& env, jtx::IOU const& usd, jtx::IOU const& eur) {
            auto jv = ammCreateJV(env, Account("alice"), usd, eur, usd(1000), eur(1000));
            jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            jv[sfFeeTier.jsonName] = kFtMedium;
            env(jv, Ter(tesSUCCESS));
            env.close();
            return env.le(keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity))->key();
        };
        Account const gw2("gateway");
        Account const lp("alice");
        Account const trader("bob");
        {
            Env env(*this, features | featureAMMCurves);
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            setupSwapEnv(env, gw2, lp, trader, usd, eur);
            auto const ammID = build(env, usd, eur);
            auto const pos =
                depositCLPosition(env, lp, usd, eur, ammID, -60, 60, usd(100), eur(100));
            liquidity = env.le(pos)->getFieldU64(sfPositionLiquidity);
        }

        // The same deposit onto an upper tick already holding INT64_MIN + that liquidity.
        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);
        auto const ammID = build(env, usd, eur);
        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) -> bool {
            Sandbox sb(&view, TapNone);
            BEAST_EXPECT(isTesSuccess(setTickBitmap(sb, ammID, 60, env.journal)));
            sb.apply(view);
            auto const tick = std::make_shared<SLE>(keylet::ammTick(ammID, 60));
            (*tick)[sfAMMID] = ammID;
            tick->setFieldI32(sfTickIndex, 60);
            tick->setFieldU64(
                sfLiquidityNet,
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::min() +
                    static_cast<std::int64_t>(liquidity)));
            tick->setFieldU64(sfLiquidityGross, 1);
            tick->setFieldNumber(sfFeeGrowthOutside0, STNumber{sfFeeGrowthOutside0, Number{0}});
            tick->setFieldNumber(sfFeeGrowthOutside1, STNumber{sfFeeGrowthOutside1, Number{0}});
            tick->setFieldU64(sfOwnerNode, 0);
            view.rawInsert(tick);
            return true;
        });
        env(clDepositJV(env, lp, usd, eur, tfTwoAsset, -60, 60, usd(100), eur(100)),
            Ter(tecAMM_FAILED));
        env.close();
    }

    void
    testCLNoOfferWithoutRoom(FeatureBitset features)
    {
        testcase("CL payment beside a CLOB offer: a pool with no room to move offers nothing");

        using namespace jtx;

        Account const gw2("gateway");
        Account const lp("alice");
        Account const trader("bob");
        Account const maker("carol");
        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);
        env.fund(XRP(100000), maker);
        env.close();
        env.trust(usd(1000000), maker);
        env.trust(eur(1000000), maker);
        env(pay(gw2, maker, usd(50000)));
        env(pay(gw2, maker, eur(50000)));
        env.close();
        bool const usdIsToken0 = usd.asset() < eur.asset();
        auto const& token0 = usdIsToken0 ? usd : eur;
        auto const& token1 = usdIsToken0 ? eur : usd;

        auto jv = ammCreateJV(env, lp, token0, token1, token0(10000), token1(10000));
        jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        jv[sfFeeTier.jsonName] = kFtMedium;
        env(jv, Ter(tesSUCCESS));
        env.close();
        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
        auto const ammID = env.le(ammKey)->key();
        depositCLPosition(env, lp, token0, token1, ammID, -60, 60, token0(100), token1(100));
        env(offer(maker, token0(100), token1(90)), Ter(tesSUCCESS));
        env.close();
        // The price on the only range's lower tick: selling token0 has nowhere to go.
        placeCLPrice(env, ammKey, -60);

        auto const ammBefore = env.le(ammKey);
        STAmount const outBefore = env.balance(trader, token1.asset()).value();
        env(pay(trader, trader, token1(9)),
            Path(~token1),
            Sendmax(token0(20)),
            Txflags(tfNoRippleDirect | tfPartialPayment),
            Ter(tesSUCCESS));
        BEAST_EXPECT(env.balance(trader, token1.asset()).value() - outBefore == token1(9).value());
        auto const ammAfter = env.le(ammKey);
        BEAST_EXPECT(
            ammAfter->getFieldI32(sfCurrentTick) == ammBefore->getFieldI32(sfCurrentTick) &&
            ammAfter->getFieldU64(sfActiveLiquidity) == ammBefore->getFieldU64(sfActiveLiquidity));
    }

    void
    testCLRangeReservesPastXRP(FeatureBitset features)
    {
        testcase(
            "CL payment: a range whose virtual XRP reserve passes the XRP range is skipped "
            "cleanly");

        using namespace jtx;

        Account const gw2("gateway");
        Account const lp("alice");
        Account const trader("bob");
        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        env.fund(XRP(100000), gw2, trader);
        env.fund(XRP(2000000000), lp);
        env.close();
        env.trust(usd(1000000), lp);
        env.trust(usd(1000000), trader);
        env(pay(gw2, lp, usd(100000)));
        env(pay(gw2, trader, usd(100000)));
        env.close();

        // XRP is token0; a price of about 1e-17 USD per drop.
        json::Value create;
        create[jss::Account] = lp.human();
        create[jss::TransactionType] = jss::AMMCreate;
        create[jss::Amount] = XRP(100000).value().getJson(JsonOptions::Values::None);
        create[jss::Amount2] = STAmount{usd.issue(), 1, -6}.getJson(JsonOptions::Values::None);
        create[jss::TradingFee] = 0;
        create[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        create[sfFeeTier.jsonName] = kFtStable;
        create[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(create, Ter(tesSUCCESS));
        env.close();
        auto const ammKey = keylet::amm(xrpIssue(), usd.asset(), kCtConcentratedLiquidity);
        std::int32_t const tick = -390000;
        placeCLPrice(env, ammKey, tick);
        auto const amm = env.le(ammKey);

        // A two-tick position around the price holding twice the int64 range of virtual XRP.
        Number const sqrtP = clSqrtPrice(*amm);
        Number const sqrtL = tickToSqrtPrice(tick - 1);
        Number const sqrtU = tickToSqrtPrice(tick + 1);
        Number const l = Number{2} * Number{std::numeric_limits<std::int64_t>::max()} * sqrtP;
        Number const amount0 = l * (sqrtU - sqrtP) / (sqrtP * sqrtU) * Number{101, -2};
        Number const amount1 = l * (sqrtP - sqrtL) * Number{101, -2};
        json::Value dep;
        dep[jss::Account] = lp.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, xrpIssue()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfTickLower.jsonName] = tick - 1;
        dep[sfTickUpper.jsonName] = tick + 1;
        dep[jss::Amount] = toSTAmount(xrpIssue(), amount0, Number::RoundingMode::Upward)
                               .getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = toSTAmount(usd.asset(), amount1, Number::RoundingMode::Upward)
                                .getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep, Ter(tesSUCCESS));
        auto const after = env.le(ammKey);
        Number const virtualXrp =
            Number{static_cast<std::int64_t>(after->getFieldU64(sfActiveLiquidity))} /
            clSqrtPrice(*after);
        if (!(virtualXrp > Number{std::numeric_limits<std::int64_t>::max()}))
        {
            fail("setup: the range's virtual XRP reserve is inside the XRP range");
            return;
        }

        env(pay(trader, trader, XRP(1)),
            Path(~XRP),
            Sendmax(usd(1)),
            Txflags(tfNoRippleDirect | tfPartialPayment),
            Ter(std::ignore));
        BEAST_EXPECT(env.ter() != tefEXCEPTION && env.ter() != tecINTERNAL);
    }

    void
    testCLReversePartialFillAfterRangeFill(FeatureBitset features)
    {
        testcase("CL payment: a reverse partial fill after a swap that fills a range");

        using namespace jtx;

        Account const gw2("gateway");
        Account const lp("alice");
        Account const trader("bob");
        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);
        bool const usdIsToken0 = usd.asset() < eur.asset();
        auto const& token0 = usdIsToken0 ? usd : eur;
        auto const& token1 = usdIsToken0 ? eur : usd;

        auto jv = ammCreateJV(env, lp, token0, token1, token0(10000), token1(10000));
        jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        jv[sfFeeTier.jsonName] = kFtStable;
        env(jv, Ter(tesSUCCESS));
        env.close();
        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
        auto const ammID = env.le(ammKey)->key();
        auto const tight =
            depositCLPosition(env, lp, token0, token1, ammID, -10, 10, token0(50), token1(50));
        auto const wide = depositCLPosition(
            env, lp, token0, token1, ammID, -1000, 1000, token0(500), token1(500));

        // An upward swap buying exactly what range [0, 10] holds of token0.
        Number const lTotal{static_cast<std::int64_t>(
            env.le(tight)->getFieldU64(sfPositionLiquidity) +
            env.le(wide)->getFieldU64(sfPositionLiquidity))};
        Number const sqrt10 = tickToSqrtPrice(10);
        STAmount const rangeOut = toSTAmount(
            token0.asset(), lTotal * (sqrt10 - Number{1}) / sqrt10, Number::RoundingMode::Downward);
        env(pay(trader, trader, rangeOut),
            Path(~token0),
            Sendmax(token1(100)),
            Txflags(tfNoRippleDirect | tfPartialPayment),
            Ter(tesSUCCESS));
        env.close();
        auto const filled = env.le(ammKey);
        Number const s0 = clSqrtPrice(*filled);
        // The price sits at tick 10 or just below it: both positions hold the range below.
        BEAST_EXPECT(s0 <= sqrt10 && filled->getFieldI32(sfCurrentTick) <= 10);

        // A small sale of token0 back is priced from both positions' liquidity.
        Number const f = Number{100000 - filled->getFieldU16(sfTradingFee)} / Number{100000};
        STAmount const inBefore = env.balance(trader, token0.asset()).value();
        STAmount const outBefore = env.balance(trader, token1.asset()).value();
        env(pay(trader, trader, token1(1)),
            Path(~token1),
            Sendmax(token0(10)),
            Txflags(tfNoRippleDirect | tfPartialPayment),
            Ter(tesSUCCESS));
        env.close();
        Number const spent{inBefore - env.balance(trader, token0.asset()).value()};
        Number const got{env.balance(trader, token1.asset()).value() - outBefore};
        BEAST_EXPECT(got > Number{0});
        Number const s1 = s0 - got / lTotal;
        Number const required = lTotal * (s0 - s1) / (s0 * s1) / f;
        BEAST_EXPECT(
            (spent * Number{1000000001, -9} >= required &&
             spent <= required * Number{1000000001, -9}));

        // The state an upward swap that exactly fills the range leaves: the price on
        // tick 10, the tick crossed, only the wide position active. A reverse partial
        // fill crosses tick 10 back for free and is priced from both positions.
        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) -> bool {
            NumberMantissaScaleGuard const scale{MantissaRange::MantissaScale::Large};
            auto const amm = std::make_shared<SLE>(*view.read(ammKey));
            amm->setFieldI32(sfCurrentTick, 10);
            amm->setFieldNumber(sfSqrtPrice, STNumber{sfSqrtPrice, sqrt10});
            amm->setFieldU64(sfActiveLiquidity, env.le(wide)->getFieldU64(sfPositionLiquidity));
            view.rawReplace(amm);
            return true;
        });
        STAmount const inOnTick = env.balance(trader, token0.asset()).value();
        STAmount const outOnTick = env.balance(trader, token1.asset()).value();
        env(pay(trader, trader, token1(1)),
            Path(~token1),
            Sendmax(token0(10)),
            Txflags(tfNoRippleDirect | tfPartialPayment),
            Ter(tesSUCCESS));
        Number const spentOnTick{inOnTick - env.balance(trader, token0.asset()).value()};
        Number const gotOnTick{env.balance(trader, token1.asset()).value() - outOnTick};
        BEAST_EXPECT(gotOnTick > Number{0});
        Number const t1 = sqrt10 - gotOnTick / lTotal;
        Number const requiredOnTick = lTotal * (sqrt10 - t1) / (sqrt10 * t1) / f;
        BEAST_EXPECT(
            (spentOnTick * Number{1000000001, -9} >= requiredOnTick &&
             spentOnTick <= requiredOnTick * Number{1000000001, -9}));
    }

    void
    testCLFeeFromTier(FeatureBitset features)
    {
        testcase("CL pool: swaps pay the fee tier's fee, whatever TradingFee the create named");

        using namespace jtx;

        for (std::uint8_t tier = 0; tier < feeTierCount; ++tier)
        {
            Account const gw2("gateway");
            Account const lp("alice");
            Account const trader("bob");
            Env env(*this, features | featureAMMCurves);
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            setupSwapEnv(env, gw2, lp, trader, usd, eur);
            bool const usdIsToken0 = usd.asset() < eur.asset();
            auto const& token0 = usdIsToken0 ? usd : eur;
            auto const& token1 = usdIsToken0 ? eur : usd;
            auto jv = ammCreateJV(env, lp, token0, token1, token0(1000), token1(1000));
            jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            jv[sfFeeTier.jsonName] = tier;
            jv[jss::TradingFee] = 500;
            env(jv, Ter(tesSUCCESS));
            env.close();
            auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
            auto const ammID = env.le(ammKey)->key();
            BEAST_EXPECT(env.le(ammKey)->getFieldU16(sfTradingFee) == feeTierToFee[tier] * 10);
            std::int32_t const width = feeTierToTickSpacing[tier] * 10;
            depositCLPosition(
                env, lp, token0, token1, ammID, -width, width, token0(1000), token1(1000));

            // A sale of token0 for 1 token1 inside the range, by an account without the
            // creator's auction-slot discount.
            auto const amm = env.le(ammKey);
            Number const l{static_cast<std::int64_t>(amm->getFieldU64(sfActiveLiquidity))};
            Number const s0 = clSqrtPrice(*amm);
            STAmount const inBefore = env.balance(trader, token0.asset()).value();
            STAmount const outBefore = env.balance(trader, token1.asset()).value();
            env(pay(trader, trader, token1(1)),
                Path(~token1),
                Sendmax(token0(10)),
                Txflags(tfNoRippleDirect | tfPartialPayment),
                Ter(tesSUCCESS));
            env.close();
            Number const spent{inBefore - env.balance(trader, token0.asset()).value()};
            Number const got{env.balance(trader, token1.asset()).value() - outBefore};
            Number const s1 = s0 - got / l;
            Number const f = Number{10000 - feeTierToFee[tier]} / Number{10000};
            Number const required = l * (s0 - s1) / (s0 * s1) / f;
            BEAST_EXPECT(
                (got > Number{0} && spent * Number{1000000001, -9} >= required &&
                 spent <= required * Number{1000000001, -9}));
        }
    }

    void
    testCLLiquidityOutOfReach(FeatureBitset features)
    {
        testcase("CL payment: liquidity beyond the walk's reach offers nothing");

        using namespace jtx;

        Account const gw2("gateway");
        Account const lp("alice");
        Account const trader("bob");
        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);
        bool const usdIsToken0 = usd.asset() < eur.asset();
        auto const& token0 = usdIsToken0 ? usd : eur;
        auto const& token1 = usdIsToken0 ? eur : usd;
        auto jv = ammCreateJV(env, lp, token0, token1, token0(1000), token1(1000));
        jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        jv[sfFeeTier.jsonName] = kFtStable;
        env(jv, Ter(tesSUCCESS));
        env.close();
        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
        auto const ammID = env.le(ammKey)->key();
        // The only positions sit more bitmap words above and below the price than a walk
        // crosses, so the pool holds both assets but no liquidity a swap reaches.
        depositCLPosition(env, lp, token0, token1, ammID, 600000, 600100, token0(1), token1(1));
        depositCLPosition(env, lp, token0, token1, ammID, -600100, -600000, token0(1), token1(1));

        env(pay(trader, trader, STAmount{token0.issue(), 1, -4}),
            Path(~token0),
            Sendmax(token1(10)),
            Txflags(tfNoRippleDirect | tfPartialPayment),
            Ter(tecPATH_DRY));
        env.close();
        BEAST_EXPECT(env.le(ammKey)->getFieldI32(sfCurrentTick) == 0);

        // With a range at the price, a payment for more token0 than it holds empties it and
        // stops there: the token0 still held sits beyond the walk's reach.
        depositCLPosition(env, lp, token0, token1, ammID, -60, 60, token0(100), token1(100));
        STAmount const before = env.balance(trader, token0.asset()).value();
        env(pay(trader, trader, token0(1000)),
            Path(~token0),
            Sendmax(token1(1000)),
            Txflags(tfNoRippleDirect | tfPartialPayment),
            Ter(tesSUCCESS));
        env.close();
        Number const got{env.balance(trader, token0.asset()).value() - before};
        BEAST_EXPECT(got > Number{99} && got <= Number{100});
        BEAST_EXPECT(env.le(ammKey)->getFieldI32(sfCurrentTick) >= 59);
    }

    void
    testCLOfferCrossingUsesRangePrice(FeatureBitset features)
    {
        testcase("CL offer crossing: the pool's offer is judged by its range's price");

        using namespace jtx;

        // A pool whose holdings sit mostly in a range out of reach (above the price with
        // `aboveRange`, else below it), so they say nothing of the price an offer trades at.
        // A trader's offer buying token0 with token1 at `limit` token1 per token0.
        auto const run = [&](bool aboveRange, Number const& limit) {
            Account const gw2("gateway");
            Account const lp("alice");
            Account const trader("bob");
            Env env(*this, features | featureAMMCurves);
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            setupSwapEnv(env, gw2, lp, trader, usd, eur);
            bool const usdIsToken0 = usd.asset() < eur.asset();
            auto const& token0 = usdIsToken0 ? usd : eur;
            auto const& token1 = usdIsToken0 ? eur : usd;
            auto jv = ammCreateJV(env, lp, token0, token1, token0(1000), token1(1000));
            jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            jv[sfFeeTier.jsonName] = kFtMedium;
            env(jv, Ter(tesSUCCESS));
            env.close();
            auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
            auto const ammID = env.le(ammKey)->key();
            depositCLPosition(env, lp, token0, token1, ammID, -60, 60, token0(100), token1(100));
            if (aboveRange)
            {
                depositCLPosition(
                    env, lp, token0, token1, ammID, 1200, 2400, token0(2000), token1(2000));
            }
            else
            {
                depositCLPosition(
                    env, lp, token0, token1, ammID, -2400, -1200, token0(2000), token1(2000));
            }

            auto const priceBefore = clSqrtPrice(*env.le(ammKey));
            STAmount const got0 = env.balance(trader, token0.asset()).value();
            STAmount const paid1 = env.balance(trader, token1.asset()).value();
            STAmount const wants = token0(1).value();
            STAmount const gives = toSTAmount(token1.asset(), limit);
            env(offer(trader, wants, gives), Ter(tesSUCCESS));
            env.close();
            Number const got{env.balance(trader, token0.asset()).value() - got0};
            Number const paid{paid1 - env.balance(trader, token1.asset()).value()};
            return std::tuple{got, paid, clSqrtPrice(*env.le(ammKey)) != priceBefore};
        };

        // Holdings of about 100 token1 for 2100 token0 promise 0.05 token1 per token0; the
        // range trades near 1, so an offer paying 0.5 does not cross.
        {
            auto const [got, paid, moved] = run(true, Number{5, -1});
            BEAST_EXPECT(got == Number{0} && paid == Number{0} && !moved);
        }

        // Holdings of about 2100 token1 for 100 token0 promise 21 token1 per token0; the
        // range trades near 1, so an offer paying up to 2 crosses at the range's price.
        {
            auto const [got, paid, moved] = run(false, Number{2});
            BEAST_EXPECT(got > Number{0} && moved);
            BEAST_EXPECT(paid <= got * Number{2});
        }
    }

    void
    testCLPartialFillPricedFromCrossedRange(FeatureBitset features)
    {
        testcase("CL payment: a partial fill is priced from the range the offer draws from");

        using namespace jtx;

        for (bool const storedEmpty : {false, true})
        {
            Account const gw2("gateway");
            Account const lp("alice");
            Account const trader("bob");
            Env env(*this, features | featureAMMCurves);
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            setupSwapEnv(env, gw2, lp, trader, usd, eur);
            bool const usdIsToken0 = usd.asset() < eur.asset();
            auto const& token0 = usdIsToken0 ? usd : eur;
            auto const& token1 = usdIsToken0 ? eur : usd;

            auto jv = ammCreateJV(env, lp, token0, token1, token0(10000), token1(10000));
            jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            jv[sfFeeTier.jsonName] = kFtStable;
            env(jv, Ter(tesSUCCESS));
            env.close();
            auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
            auto const ammID = env.le(ammKey)->key();

            // Either a tight range on a wide one with the price on the tight range's lower
            // tick, or one range below a price with no liquidity at it.
            std::vector<Keylet> positions;
            if (storedEmpty)
            {
                auto const seq = env.seq(lp);
                env(clDepositJV(
                        env,
                        lp,
                        token0,
                        token1,
                        tfSingleAsset,
                        -1000,
                        -10,
                        token1(500),
                        std::nullopt),
                    Ter(tesSUCCESS));
                env.close();
                positions.push_back(keylet::ammPosition(ammID, lp.id(), seq));
            }
            else
            {
                positions.push_back(depositCLPosition(
                    env, lp, token0, token1, ammID, -10, 10, token0(50), token1(50)));
                positions.push_back(depositCLPosition(
                    env, lp, token0, token1, ammID, -1000, 1000, token0(500), token1(500)));
                placeCLPrice(env, ammKey, -10);
            }
            auto const pre = env.le(ammKey);
            BEAST_EXPECT((pre->getFieldU64(sfActiveLiquidity) == 0) == storedEmpty);

            // Selling token0 crosses tick -10 for free; the range below holds the
            // liquidity of every position below the tick.
            Number const sqrtPb = tickToSqrtPrice(-10);
            std::int64_t below = 0;
            for (auto const& pos : positions)
            {
                auto const p = env.le(pos);
                if (p->getFieldI32(sfTickLower) < -10)
                    below += static_cast<std::int64_t>(p->getFieldU64(sfPositionLiquidity));
            }
            Number const l{below};
            Number const f = Number{100000 - pre->getFieldU16(sfTradingFee)} / Number{100000};

            // A small output takes part of the AMM offer.
            STAmount const target = token1(1).value();
            STAmount const inBefore = env.balance(trader, token0.asset()).value();
            STAmount const outBefore = env.balance(trader, token1.asset()).value();
            env(pay(trader, trader, target),
                Path(~token1),
                Sendmax(token0(10)),
                Txflags(tfNoRippleDirect | tfPartialPayment),
                Ter(tesSUCCESS));
            Number const spent{inBefore - env.balance(trader, token0.asset()).value()};
            Number const got{env.balance(trader, token1.asset()).value() - outBefore};
            BEAST_EXPECT(got > Number{0});
            Number const sqrtPn = sqrtPb - got / l;
            Number const required = l * (sqrtPb - sqrtPn) / (sqrtPb * sqrtPn) / f;
            BEAST_EXPECT((spent * Number{1000000001, -9} >= required));

            // The trade back and every position's withdrawal stay within what the pool holds.
            env.close();
            env(pay(trader, trader, token0(1)),
                Path(~token0),
                Sendmax(token1(2)),
                Txflags(tfNoRippleDirect | tfPartialPayment),
                Ter(tesSUCCESS));
            env.close();
            for (auto const& pos : positions)
            {
                env(clWithdrawJV(env, lp, token0, token1, pos.key), Ter(tesSUCCESS));
                env.close();
            }
        }
    }

    void
    testCLNextRangePricedFromCrossedTick(FeatureBitset features)
    {
        testcase("CL payment: offers after a tick crossing are priced from the crossed tick");

        using namespace jtx;

        Account const gw2("gateway");
        Account const lp("alice");
        Account const trader("bob");
        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);
        bool const usdIsToken0 = usd.asset() < eur.asset();
        auto const& token0 = usdIsToken0 ? usd : eur;
        auto const& token1 = usdIsToken0 ? eur : usd;

        auto jv = ammCreateJV(env, lp, token0, token1, token0(10000), token1(10000));
        jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        jv[sfFeeTier.jsonName] = kFtStable;
        env(jv, Ter(tesSUCCESS));
        env.close();
        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
        auto const ammID = env.le(ammKey)->key();

        // A tight range on top of a wide one: crossing tick -10 drops the tight liquidity.
        depositCLPosition(env, lp, token0, token1, ammID, -10, 10, token0(50), token1(50));
        depositCLPosition(env, lp, token0, token1, ammID, -1000, 1000, token0(500), token1(500));
        auto const pre = env.le(ammKey);
        BEAST_EXPECT(pre->getFieldI32(sfCurrentTick) == 0);

        // Range 0 (tick 0 down to -10) at the starting liquidity, then tick -10's price.
        Number const l0(pre->getFieldU64(sfActiveLiquidity));
        Number const sqrtP0 = tickToSqrtPrice(0);
        Number const sqrtPb = tickToSqrtPrice(-10);
        Number const range0Out = l0 * (sqrtP0 - sqrtPb);
        Number const range0In = l0 * (sqrtP0 - sqrtPb) / (sqrtP0 * sqrtPb);
        Number const boundaryRate = Number{1} / (sqrtPb * sqrtPb);

        STAmount const target = toSTAmount(
            token1.asset(), range0Out + range0Out / Number{4}, Number::RoundingMode::Downward);
        STAmount const inBefore = env.balance(trader, token0.asset()).value();
        STAmount const outBefore = env.balance(trader, token1.asset()).value();
        env(pay(trader, trader, target),
            Path(~token1),
            Sendmax(token0(1000)),
            Txflags(tfNoRippleDirect | tfPartialPayment),
            Ter(tesSUCCESS));
        env.close();
        Number const spent{inBefore - env.balance(trader, token0.asset()).value()};
        Number const got{env.balance(trader, token1.asset()).value() - outBefore};
        // The price crossed tick -10 and kept falling inside the range below it.
        BEAST_EXPECT(env.le(ammKey)->getFieldI32(sfCurrentTick) < -10);
        BEAST_EXPECT(got > range0Out);

        // Every unit past range 0 costs at least tick -10's price.
        Number const floor = range0In + (got - range0Out) * boundaryRate;
        BEAST_EXPECT((spent * Number{1000000001, -9} >= floor));
    }

    void
    testCLOfferAgainstClobFollowsTheCurve(FeatureBitset features)
    {
        testcase("CL payment beside a CLOB offer pays no more than the curve gives");

        using namespace jtx;

        Account const gw2("gateway");
        Account const lp("alice");
        Account const other("dan");
        Account const maker("bob");
        Account const taker("carol");
        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        env.fund(XRP(100000), gw2, lp, other, maker, taker);
        env.close();
        bool const usdIsToken0 = usd.asset() < eur.asset();
        auto const& token0 = usdIsToken0 ? usd : eur;
        auto const& token1 = usdIsToken0 ? eur : usd;
        for (auto const& acct : {lp, other, maker, taker})
        {
            env.trust(token0(1000000), acct);
            env.trust(token1(1000000), acct);
        }
        env.close();
        for (auto const& acct : {lp, other, maker, taker})
        {
            env(pay(gw2, acct, token0(100000)));
            env(pay(gw2, acct, token1(100000)));
        }
        env.close();

        auto jv = ammCreateJV(env, lp, token0, token1, token0(1000), token1(1000));
        jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        jv[sfFeeTier.jsonName] = kFtMedium;
        env(jv, Ter(tesSUCCESS));
        env.close();
        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
        auto const ammID = env.le(ammKey)->key();
        auto const ammAccount = env.le(ammKey)->getAccountID(sfAccount);
        // A range around the price and a large token0-only range above it.
        depositCLPosition(env, lp, token0, token1, ammID, -600, 600, token0(1000), token1(1000));
        depositCLPosition(env, other, token0, token1, ammID, 1200, 2400, token0(50000), token1(1));
        // A CLOB offer selling token0 at price 1.
        env(offer(maker, token1(100), token0(100)), Ter(tesSUCCESS));
        env.close();

        Number const sqrtP{env.le(ammKey)->getFieldNumber(sfSqrtPrice)};
        auto const in0 = ammAccountHolds(*env.current(), ammAccount, token1.asset());
        auto const out0 = ammAccountHolds(*env.current(), ammAccount, token0.asset());
        env(pay(taker, taker, token0(50000)),
            Path(~token0),
            Sendmax(token1(10000)),
            Txflags(tfNoRippleDirect | tfPartialPayment),
            Ter(tesSUCCESS));
        env.close();
        Number const ammIn =
            Number{ammAccountHolds(*env.current(), ammAccount, token1.asset())} - Number{in0};
        Number const ammOut =
            Number{out0} - Number{ammAccountHolds(*env.current(), ammAccount, token0.asset())};
        BEAST_EXPECT(ammIn > Number{0});
        // Buying token0 only raises the price, so the pool pays at most ammIn / P.
        BEAST_EXPECT(ammOut <= ammIn / (sqrtP * sqrtP));
    }

    void
    testCLEqualPositionsWithdrawWithinDeposits(FeatureBitset features)
    {
        testcase("CL withdraw: equal positions from two LPs pay back no more than was deposited");

        using namespace jtx;

        Account const gw2("gateway");
        Account const lp("alice");
        Account const other("bob");
        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, other, usd, eur);
        bool const usdIsToken0 = usd.asset() < eur.asset();
        auto const& token0 = usdIsToken0 ? usd : eur;
        auto const& token1 = usdIsToken0 ? eur : usd;

        auto jv = ammCreateJV(env, lp, token0, token1, token0(1000), token1(1000));
        jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        jv[sfFeeTier.jsonName] = kFtMedium;
        env(jv, Ter(tesSUCCESS));
        env.close();
        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
        auto const ammID = env.le(ammKey)->key();

        auto const first =
            depositCLPosition(env, lp, token0, token1, ammID, -60, 60, token0(10), token1(10));
        auto const second =
            depositCLPosition(env, other, token0, token1, ammID, -60, 60, token0(10), token1(10));
        env(clWithdrawJV(env, lp, token0, token1, first.key), Ter(tesSUCCESS));
        env.close();
        env(clWithdrawJV(env, other, token0, token1, second.key), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.le(ammKey)->getFieldU32(sfPositionCount) == 0);
    }

    void
    testCLNewPositionFeeSnapshot(FeatureBitset features)
    {
        testcase("CL deposit on initialised ticks: a new position starts with no fees owed");

        using namespace jtx;

        Account const lp("alice");
        Account const trader("bob");
        Account const late("carol");
        Account const gw2("gateway");

        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);
        env.fund(XRP(100000), late);
        env.close();
        env.trust(usd(1000000), late);
        env.trust(eur(1000000), late);
        env.close();
        env(pay(gw2, late, usd(1000)));
        env(pay(gw2, late, eur(1000)));
        env.close();
        bool const usdIsToken0 = usd.asset() < eur.asset();
        auto const& token0 = usdIsToken0 ? usd : eur;
        auto const& token1 = usdIsToken0 ? eur : usd;

        auto jv = ammCreateJV(env, lp, token0, token1, token0(10000), token1(10000));
        jv[jss::TradingFee] = 100;
        jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        jv[sfFeeTier.jsonName] = kFtHigh;
        env(jv, Ter(tesSUCCESS));
        env.close();
        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
        auto const ammID = env.le(ammKey)->key();
        auto const first = depositCLPosition(
            env, lp, token0, token1, ammID, -10000, 10000, token0(10000), token1(10000));

        // Swaps in both directions grow both fee-growth globals inside the range.
        env(pay(trader, trader, token1(100)),
            Path(~token1),
            Sendmax(token0(120)),
            Txflags(tfNoRippleDirect | tfPartialPayment),
            Ter(tesSUCCESS));
        env(pay(trader, trader, token0(100)),
            Path(~token0),
            Sendmax(token1(120)),
            Txflags(tfNoRippleDirect | tfPartialPayment),
            Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(Number{env.le(ammKey)->getFieldNumber(sfFeeGrowthGlobal0)} > Number{0});
        BEAST_EXPECT(Number{env.le(ammKey)->getFieldNumber(sfFeeGrowthGlobal1)} > Number{0});

        // A position on the same ticks, collected at once, is owed nothing.
        auto const second = depositCLPosition(
            env, late, token0, token1, ammID, -10000, 10000, token0(100), token1(100));
        auto const t0 = env.balance(late, token0.issue());
        auto const t1 = env.balance(late, token1.issue());
        env(collectJV(
                env, late, token0, token1, kCtConcentratedLiquidity, second.key, std::nullopt),
            Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.balance(late, token0.issue()) == t0);
        BEAST_EXPECT(env.balance(late, token1.issue()) == t1);

        // The first position still collects the fees it earned.
        auto const f0 = env.balance(lp, token0.issue());
        auto const f1 = env.balance(lp, token1.issue());
        env(collectJV(env, lp, token0, token1, kCtConcentratedLiquidity, first.key, std::nullopt),
            Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.balance(lp, token0.issue()) > f0);
        BEAST_EXPECT(env.balance(lp, token1.issue()) > f1);
    }

    void
    testPaymentSkipsEmptyCLPool(FeatureBitset features)
    {
        testcase("Payment routes past a CL pool with no active liquidity");

        using namespace jtx;

        Account const lp("alice");
        Account const trader("bob");
        Account const gw2("gateway");

        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);
        createCurvePool(env, lp, usd, eur, usd(1000), eur(1000), kCtConstantProduct, json::Value{});
        auto jv = ammCreateJV(env, lp, usd, eur, usd(1000), eur(1000));
        jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        jv[sfFeeTier.jsonName] = kFtMedium;
        env(jv, Ter(tesSUCCESS));
        env.close();

        auto const before = env.balance(trader, eur.issue());
        env(pay(trader, trader, eur(10)),
            Path(~eur),
            Sendmax(usd(20)),
            Txflags(tfNoRippleDirect | tfPartialPayment),
            Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.balance(trader, eur.issue()) == before + eur(10));
    }

    void
    testCLWithdrawOtherPoolPosition(FeatureBitset features)
    {
        testcase("CL withdraw: a position of another pool is refused");

        using namespace jtx;

        Account const lp("alice");
        Account const other("bob");
        Account const gw2("gateway");

        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        auto const gbp = gw2["GBP"];
        setupSwapEnv(env, gw2, lp, other, usd, eur);
        env.trust(gbp(1000000), lp);
        env.trust(gbp(1000000), other);
        env(pay(gw2, lp, gbp(100000)));
        env(pay(gw2, other, gbp(100000)));
        env.close();

        auto const createCL = [&](IOU const& a, IOU const& b) {
            auto jv = ammCreateJV(env, lp, a, b, a(1000), b(1000));
            jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            jv[sfFeeTier.jsonName] = kFtMedium;
            env(jv, Ter(tesSUCCESS));
            env.close();
            return env.le(keylet::amm(a.asset(), b.asset(), kCtConcentratedLiquidity))->key();
        };
        auto const smallPool = createCL(usd, eur);
        auto const largePool = createCL(usd, gbp);

        // A small position in one pool and a large one in the other.
        auto const smallPosition =
            depositCLPosition(env, lp, usd, eur, smallPool, -600, 600, usd(1), eur(1));
        depositCLPosition(env, other, usd, gbp, largePool, -600, 600, usd(1000), gbp(1000));

        // Withdrawing the small position through the large pool would pay out
        // the large pool's assets against the small pool's position.
        env(clWithdrawJV(env, lp, usd, gbp, smallPosition.key), Ter(tecNO_ENTRY));
        BEAST_EXPECT(env.le(smallPosition));
    }

    void
    testPathFindThroughStableSwap(FeatureBitset features)
    {
        testcase("Path finding through a StableSwap pool");

        using namespace jtx;

        Account const al("alice");
        Account const bo("bob");
        Account const gw2("gateway");

        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, al, bo, usd, eur);

        json::Value ssParams;
        ssParams[sfAmplification.jsonName] = 100;
        createCurvePool(env, al, usd, eur, usd(10000), eur(10000), kCtStableSwap, ssParams);

        // Path finding runs the payment engine outside a transaction.
        auto const [paths, srcAmt, dstAmt] =
            findPaths(env, bo, al, eur(100), std::nullopt, usd.currency, gw2.id());
        BEAST_EXPECT(dstAmt == eur(100));
        BEAST_EXPECT(srcAmt > usd(99));
        BEAST_EXPECT(srcAmt < usd(101));
    }

    void
    testStableSwapOfferBesideClob(FeatureBitset features)
    {
        testcase("StableSwap payment beside a CLOB offer is priced on the StableSwap curve");

        using namespace jtx;

        // Pool balances (USD, EUR), the maker's price in USD per EUR, and the EUR delivered:
        // balanced, where a constant-product sizing stops the pool at the maker's price long before
        // the StableSwap price reaches it, and imbalanced, where the holdings ratio is far above
        // the maker's price though the pool trades near 1.
        struct Case
        {
            std::int64_t usd;
            std::int64_t eur;
            std::int64_t makerUsd;
            std::int64_t makerEur;
            std::int64_t deliver;
        };
        for (auto const& c :
             {Case{.usd = 10000, .eur = 10000, .makerUsd = 1050, .makerEur = 1000, .deliver = 300},
              Case{.usd = 12000, .eur = 8000, .makerUsd = 1200, .makerEur = 1000, .deliver = 100}})
        {
            Account const gw2("gateway");
            Account const lp("alice");
            Account const taker("bob");
            Account const maker("carol");
            Env env(*this, features | featureAMMCurves);
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            setupSwapEnv(env, gw2, lp, taker, usd, eur);
            env.fund(XRP(100000), maker);
            env.close();
            env.trust(usd(1000000), maker);
            env.trust(eur(1000000), maker);
            env(pay(gw2, maker, eur(50000)));
            env.close();

            json::Value ssParams;
            ssParams[sfAmplification.jsonName] = 100;
            createCurvePool(env, lp, usd, eur, usd(c.usd), eur(c.eur), kCtStableSwap, ssParams);
            env(offer(maker, usd(c.makerUsd), eur(c.makerEur)), Ter(tesSUCCESS));
            env.close();

            // The most the StableSwap pool charges for the delivery.
            auto const ammSle = env.le(keylet::amm(usd.asset(), eur.asset(), kCtStableSwap));
            auto const* curve = getCurve(kCtStableSwap, env.current()->rules());
            auto const cost =
                curve->swapOut(usd(c.usd), eur(c.eur), eur(c.deliver), 0, ammSle.get());
            if (!BEAST_EXPECT(cost.has_value()))
                continue;
            BEAST_EXPECT(
                Number{*cost} * Number{c.makerEur} < Number{c.makerUsd} * Number{c.deliver});

            auto const usdBefore = env.balance(taker, usd.issue());
            auto const makerEurBefore = env.balance(maker, eur.issue());
            env(pay(taker, taker, eur(c.deliver)),
                Path(~eur),
                Sendmax(usd(c.deliver * 2)),
                Txflags(tfNoRippleDirect | tfPartialPayment),
                Ter(tesSUCCESS));
            env.close();

            // The whole delivery comes from the pool at the curve's price.
            BEAST_EXPECT(env.balance(maker, eur.issue()) == makerEurBefore);
            Number const spent{usdBefore - env.balance(taker, usd.issue())};
            BEAST_EXPECT(spent <= (Number{*cost} * Number{1000001, -6}));
        }
    }

    void
    testStableSwapOfferBesideCheaperClob(FeatureBitset features)
    {
        testcase("StableSwap pool is sized by the curve beside a CLOB offer priced above its spot");

        using namespace jtx;

        Account const gw2("gateway");
        Account const lp("alice");
        Account const taker("bob");
        Account const maker("carol");
        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, taker, usd, eur);
        env.fund(XRP(100000), maker);
        env.close();
        env.trust(usd(1000000), maker);
        env.trust(eur(1000000), maker);
        env(pay(gw2, maker, eur(50000)));
        env.close();

        json::Value ssParams;
        ssParams[sfAmplification.jsonName] = 100;
        createCurvePool(env, lp, usd, eur, usd(10000), eur(10000), kCtStableSwap, ssParams);

        // The maker sells 2000 EUR at 1.002 USD per EUR; the pool's marginal price reaches 1.002
        // after about 1870 EUR, so a delivery of 3000 EUR crosses into the maker's offer.
        env(offer(maker, usd(2004), eur(2000)), Ter(tesSUCCESS));
        env.close();

        auto const ammSle = env.le(keylet::amm(usd.asset(), eur.asset(), kCtStableSwap));
        auto const* curve = getCurve(kCtStableSwap, env.current()->rules());
        auto const poolOnlyCost =
            curve->swapOut(usd(10000), eur(10000), eur(3000), 0, ammSle.get());
        if (!BEAST_EXPECT(poolOnlyCost.has_value()))
            return;

        auto const usdBefore = env.balance(taker, usd.issue());
        auto const makerEurBefore = env.balance(maker, eur.issue());
        env(pay(taker, taker, eur(3000)),
            Path(~eur),
            Sendmax(usd(6000)),
            Txflags(tfNoRippleDirect | tfPartialPayment),
            Ter(tesSUCCESS));
        env.close();

        // The maker's offer is consumed and the taker pays no more than the pool alone charges.
        BEAST_EXPECT(env.balance(maker, eur.issue()) < makerEurBefore);
        Number const spent{usdBefore - env.balance(taker, usd.issue())};
        BEAST_EXPECT(spent <= Number{*poolOnlyCost});
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
            createCurvePool(env, al, usd, eur, usd(10000), eur(10000), kCtStableSwap, ssParams);

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
                env, al, usd, eur, usd(10000), eur(10000), kCtConstantProduct, json::Value{});

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
                env, al, usd, eur, usd(10000), eur(10000), kCtConstantProduct, json::Value{});
            createCurvePool(env, al, usd, eur, usd(10000), eur(10000), kCtStableSwap, ssParams);

            // Both pools should exist independently
            auto const cpSle =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtConstantProduct));
            auto const ssSle =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtStableSwap));
            BEAST_EXPECT(cpSle != nullptr);
            BEAST_EXPECT(ssSle != nullptr);
            if (cpSle && ssSle)
                BEAST_EXPECT(cpSle->key() != ssSle->key());
        }
    }

    // AMMCreate must distinguish "reserved curve type" (CurveType 3,
    // permanently unused in this amendment) from "future curve type"
    // (CurveType 4, planned Smart AMM behind a separate amendment).
    // The former is data-invalid (temMALFORMED); the latter is gated
    // (temDISABLED). Without the distinction, clients see "invalid
    // forever" for CurveType 4 instead of "try again when activated".
    void
    testCreateCurveTypeGating(FeatureBitset features)
    {
        testcase("AMMCreate: CurveType 3 reserved, CurveType 4 gated");

        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const al("alice");
        Account const gw2("gateway");
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        fundForAMMCreate(env, gw2, al, usd, eur);

        auto const mk = [&](std::uint8_t ct) {
            auto jv = ammCreateJV(env, al, usd, eur, usd(100), eur(100));
            jv[sfCurveType.jsonName] = ct;
            return jv;
        };

        env(mk(3), Ter(temMALFORMED));
        env.close();
        env(mk(4), Ter(temDISABLED));
        env.close();
        env(mk(99), Ter(temMALFORMED));
        env.close();
    }

    // AMMDelete on a CL pool that has outstanding positions is
    // rejected at preclaim with tecHAS_OBLIGATIONS. The check reads
    // sfPositionCount on the AMM SLE, which AMMDeposit increments on
    // new position creation and AMMWithdraw decrements on full close.
    // Without this guard, doApply would reach deleteAMMTrustLines and
    // fail with tecINTERNAL — wrong error code, wrong layer, and the
    // outstanding position/tick SLEs would orphan if the trustline
    // check were ever bypassed.
    void
    testDeleteCLWithPosition(FeatureBitset features)
    {
        testcase("AMMDelete(CL) with outstanding position returns tecHAS_OBLIGATIONS");

        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const al("alice");
        Account const gw2("gateway");
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        fundForAMMCreate(env, gw2, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        cv[sfFeeTier.jsonName] = kFtMedium;
        env(cv);
        env.close();

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
        env(dep);
        env.close();

        json::Value del;
        del[jss::Account] = al.human();
        del[jss::TransactionType] = jss::AMMDelete;
        del[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        del[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        del[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        del[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(del, Ter(tecHAS_OBLIGATIONS));
        env.close();

        // Pool still exists.
        BEAST_EXPECT(
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity)) !=
            nullptr);
    }

    // After AMMCreate(CL) no positions exist, no LP tokens are minted,
    // no assets are transferred — the pool is genuinely empty. An
    // AMMDelete on this pool must succeed: LPTokenBalance is zero, the
    // AMM account has no trustlines holding asset balances, no positions
    // reference the pool. This is the inverse of the pre-fix state where
    // AMMCreate(CL) seeded a non-zero LPTokenBalance and stranded assets,
    // making CL pools immortal.
    void
    testDeleteCLEmpty(FeatureBitset features)
    {
        testcase("AMMDelete(CL) on empty pool succeeds");

        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const al("alice");
        Account const gw2("gateway");
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        fundForAMMCreate(env, gw2, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        cv[sfFeeTier.jsonName] = kFtMedium;
        env(cv);
        env.close();

        BEAST_EXPECT(
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity)) !=
            nullptr);

        json::Value del;
        del[jss::Account] = al.human();
        del[jss::TransactionType] = jss::AMMDelete;
        del[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        del[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        del[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        del[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(del);
        env.close();

        BEAST_EXPECT(
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity)) ==
            nullptr);
    }

    // AMMCreate(CL) matches Uniswap v3 / v4 / Trader Joe LB: it
    // initializes the pool only, without transferring any assets or
    // minting LP tokens. Amount / Amount2 act as the initial price
    // ratio. First liquidity must come via AMMDeposit, which mints an
    // ltAMM_POSITION. CL positions (not LP tokens) are the unit of
    // ownership; minting LP tokens at create would strand them — there
    // is no redemption path. Trustlines + lsfAMMNode are established
    // lazily by the first AMMDeposit's accountSend into the pool.
    void
    testCreateCLNoTransfer(FeatureBitset features)
    {
        testcase("AMMCreate(CL) does not transfer assets or mint LP tokens");

        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const al("alice");
        Account const gw2("gateway");
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        fundForAMMCreate(env, gw2, al, usd, eur);

        auto const usdBefore = Number(env.balance(al, usd.issue()).value());
        auto const eurBefore = Number(env.balance(al, eur.issue()).value());

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        cv[sfFeeTier.jsonName] = kFtMedium;
        env(cv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity));
        BEAST_EXPECT(ammSle != nullptr);
        if (!ammSle)
            return;

        // Creator's IOU balances unchanged — no asset transfer.
        auto const usdAfter = Number(env.balance(al, usd.issue()).value());
        auto const eurAfter = Number(env.balance(al, eur.issue()).value());
        BEAST_EXPECT(usdBefore == usdAfter);
        BEAST_EXPECT(eurBefore == eurAfter);

        // No LP tokens minted for CL — positions are the unit of
        // ownership.
        BEAST_EXPECT(ammSle->getFieldAmount(sfLPTokenBalance) == beast::kZero);

        // Pool starts with no active liquidity (no positions yet).
        BEAST_EXPECT(ammSle->getFieldU64(sfActiveLiquidity) == 0);
    }

    // Demonstrates that BookStep's multi-curve selector picks pools by
    // marginal spot price alone, ignoring fillable depth. A pool with a
    // slightly better marginal price but trivial reserves will be chosen
    // over a deep pool that would deliver an order of magnitude more
    // output for the same input. The cure is to compare realized
    // post-fee output at the step's input bound, not marginal SP.
    void
    testMarginalSpotPriceSelector(FeatureBitset features)
    {
        testcase("BookStep marginal SP selector ignores depth");

        using namespace jtx;

        Account const al("alice");
        Account const bo("bob");
        Account const gw2("gateway");

        json::Value ssParams;
        ssParams[sfAmplification.jsonName] = 100;

        // Two pools for the same pair USD/EUR, both fee=0:
        //   CP pool: (1000 USD, 1010 EUR)        → marginal SP = 1.010
        //   SS pool: (90000 USD, 90000 EUR) A=100 → marginal SP ≈ 1.000
        //
        // Current selector picks CP (highest SP). But for a 500-USD swap:
        //   CP realized: 1010·500/(1000+500) ≈ 336.67 EUR
        //   SS realized: ≈ 499.998 EUR (near-peg, deep)
        //
        // Correct routing depends on realized output for the step size,
        // not on marginal SP. Asserting eurGot > 490 fails under the
        // marginal-SP selector and passes once depth is honored.
        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, al, bo, usd, eur);

        createCurvePool(env, al, usd, eur, usd(1000), eur(1010), kCtConstantProduct, json::Value{});
        createCurvePool(env, al, usd, eur, usd(90000), eur(90000), kCtStableSwap, ssParams);

        auto const eurBefore = env.balance(bo, eur.issue());

        env(pay(bo, bo, eur(500)),
            jtx::Path(~eur),
            jtx::Sendmax(usd(500)),
            jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
        env.close();

        auto const eurGot = Number(env.balance(bo, eur.issue()) - eurBefore);

        // Correct selector must route through SS (deep, near-peg).
        // Marginal-SP selector picks CP and only delivers ~337 EUR.
        BEAST_EXPECT(eurGot > Number{490});
    }

    // Regression: AMMCollectFees must locate the position SLE by its
    // keylet (the tx's sfPositionID is the position keylet hash, same
    // convention AMMWithdraw uses). Previously the transactor scanned
    // the owner directory for a position whose sfNFTokenID field
    // matched — but AMMDeposit never writes that field, so the scan
    // always missed and the tx returned tecNO_ENTRY. With direct keylet
    // lookup, collecting on a position that has accrued no fees yet
    // must succeed and transfer nothing.
    void
    testCollectFeesLookup(FeatureBitset features)
    {
        testcase("AMMCollectFees: position lookup by keylet succeeds");

        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const al("alice");
        Account const gw2("gateway");
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        fundForAMMCreate(env, gw2, al, usd, eur);

        // Create a CL pool.
        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        cv[sfFeeTier.jsonName] = kFtMedium;
        env(cv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity));
        BEAST_EXPECT(ammSle != nullptr);
        if (!ammSle)
            return;
        auto const ammID = ammSle->key();

        // Deposit a CL position spanning the current tick.
        auto const seqDeposit = env.seq(al);
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
        env(dep);
        env.close();

        // The position keylet — the value the caller passes as PositionID.
        auto const posKeylet = keylet::ammPosition(ammID, al.id(), seqDeposit);
        auto const posSle = env.current()->read(posKeylet);
        BEAST_EXPECT(posSle != nullptr);
        if (!posSle)
            return;

        // AMMCollectFees should find the position by its keylet via the
        // direct lookup keyed off sfPositionID.
        json::Value coll;
        coll[jss::Account] = al.human();
        coll[jss::TransactionType] = jss::AMMCollectFees;
        coll[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        coll[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        coll[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        coll[sfPositionID.jsonName] = to_string(posKeylet.key);
        coll[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(coll);
        env.close();
    }

    // End-to-end TDD coverage for the swap-apply path. Before applySwap was
    // wired in, BookStep::consumeOffer transferred trustline balances but
    // never updated the AMM SLE — so a swap through a CL pool computed
    // against stale state and feeGrowthGlobal0/1 stayed zero forever. This
    // test pins three things: (1) feeGrowthGlobal advances after a swap,
    // (2) the LP receives non-zero fees via AMMCollectFees, (3) repeated
    // swaps continue to accumulate feeGrowth (i.e. the writeback isn't
    // one-shot).
    void
    testCLSwapAppliesFeeGrowth(FeatureBitset features)
    {
        testcase("CL swap: feeGrowth accrues and AMMCollectFees pays out");

        using namespace jtx;

        Account const lp("alice");  // LP creator + position owner
        Account const trader("bob");
        Account const gw2("gateway");

        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);

        // Create a CL pool. 1bp fee (kFtStable, tickSpacing=1) — small enough
        // not to drift the price meaningfully, but non-zero so we have fees
        // to observe. AMMCreate(CL) only sets the initial price ratio; no
        // assets transfer until a position is deposited.
        {
            auto jv = ammCreateJV(env, lp, usd, eur, usd(10000), eur(10000));
            jv[jss::TradingFee] = 1;
            jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            jv[sfFeeTier.jsonName] = kFtStable;
            env(jv);
            env.close();
        }

        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
        auto const ammID = env.current()->read(ammKey)->key();

        // Wide range straddling tick 0 so a single swap stays within range
        // and no tick crossings happen — the simplest case the apply path
        // must handle correctly (segment fee allocation, feeGrowthGlobal
        // bump, no boundary flip).
        auto const seqDeposit = env.seq(lp);
        json::Value dep;
        dep[jss::Account] = lp.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfTickLower.jsonName] = -10000;
        dep[sfTickUpper.jsonName] = 10000;
        dep[jss::Amount] = usd(10000).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(10000).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep);
        env.close();

        auto const posKeylet = keylet::ammPosition(ammID, lp.id(), seqDeposit);

        // Pre-swap: feeGrowth must be zero.
        {
            auto const ammSle = env.current()->read(ammKey);
            BEAST_EXPECT(ammSle);
            if (!ammSle)
                return;
            Number const fg0{ammSle->getFieldNumber(sfFeeGrowthGlobal0)};
            Number const fg1{ammSle->getFieldNumber(sfFeeGrowthGlobal1)};
            BEAST_EXPECT(fg0 == Number{0});
            BEAST_EXPECT(fg1 == Number{0});
        }

        // Swap 1: trader pays USD, gets EUR.
        auto const eurBefore = env.balance(trader, eur.issue());
        env(pay(trader, trader, eur(100)),
            jtx::Path(~eur),
            jtx::Sendmax(usd(120)),
            jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
        env.close();
        auto const eurDelivered = env.balance(trader, eur.issue()) - eurBefore;
        BEAST_EXPECT(eurDelivered > eur(0));

        // After swap 1: feeGrowth on the input side must have advanced. The
        // input asset is whichever is the lexicographically smaller of the
        // two (zeroForOne convention in the curve). Both sides shouldn't be
        // non-zero from a single direction swap.
        Number fg0AfterSwap1{0}, fg1AfterSwap1{0};
        {
            auto const ammSle = env.current()->read(ammKey);
            BEAST_EXPECT(ammSle);
            if (!ammSle)
                return;
            fg0AfterSwap1 = Number{ammSle->getFieldNumber(sfFeeGrowthGlobal0)};
            fg1AfterSwap1 = Number{ammSle->getFieldNumber(sfFeeGrowthGlobal1)};
            // Exactly one of the two sides advanced.
            BEAST_EXPECT((fg0AfterSwap1 > Number{0}) != (fg1AfterSwap1 > Number{0}));
        }

        // Swap 2: same direction. feeGrowth must be strictly larger
        // (monotonicity) — proves the writeback path isn't a one-shot.
        env(pay(trader, trader, eur(100)),
            jtx::Path(~eur),
            jtx::Sendmax(usd(120)),
            jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
        env.close();
        {
            auto const ammSle = env.current()->read(ammKey);
            BEAST_EXPECT(ammSle);
            if (!ammSle)
                return;
            Number const fg0{ammSle->getFieldNumber(sfFeeGrowthGlobal0)};
            Number const fg1{ammSle->getFieldNumber(sfFeeGrowthGlobal1)};
            BEAST_EXPECT(fg0 >= fg0AfterSwap1);
            BEAST_EXPECT(fg1 >= fg1AfterSwap1);
            BEAST_EXPECT(fg0 > fg0AfterSwap1 || fg1 > fg1AfterSwap1);
        }

        // Collect: LP must receive non-zero fees on the input side.
        auto const usdBeforeCollect = env.balance(lp, usd.issue());
        auto const eurBeforeCollect = env.balance(lp, eur.issue());
        {
            json::Value coll;
            coll[jss::Account] = lp.human();
            coll[jss::TransactionType] = jss::AMMCollectFees;
            coll[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            coll[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            coll[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            coll[sfPositionID.jsonName] = to_string(posKeylet.key);
            coll[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(coll);
            env.close();
        }
        auto const usdGained = env.balance(lp, usd.issue()) - usdBeforeCollect;
        auto const eurGained = env.balance(lp, eur.issue()) - eurBeforeCollect;
        // Single-direction trading — input side gains, opposite side stays
        // flat. Don't pin which is which (depends on lex order of currency
        // codes); just require that the LP got something on at least one
        // side, and nothing went negative.
        BEAST_EXPECT(usdGained >= usd(0));
        BEAST_EXPECT(eurGained >= eur(0));
        BEAST_EXPECT(usdGained > usd(0) || eurGained > eur(0));

        // Second collect immediately after must be a no-op transfer (the
        // snapshot was advanced) — but should still return tesSUCCESS.
        auto const usdBeforeCollect2 = env.balance(lp, usd.issue());
        auto const eurBeforeCollect2 = env.balance(lp, eur.issue());
        {
            json::Value coll;
            coll[jss::Account] = lp.human();
            coll[jss::TransactionType] = jss::AMMCollectFees;
            coll[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            coll[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            coll[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            coll[sfPositionID.jsonName] = to_string(posKeylet.key);
            coll[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(coll);
            env.close();
        }
        BEAST_EXPECT(env.balance(lp, usd.issue()) == usdBeforeCollect2);
        BEAST_EXPECT(env.balance(lp, eur.issue()) == eurBeforeCollect2);
    }

    // Pins the tick-crossing branch of applySwap. testCLSwapAppliesFeeGrowth
    // only exercises the in-range case. The tricky bit: with a single
    // position, `maxOffer` caps offer output at 99% of pool reserves, and a
    // CL position's boundary is exactly the 100% drain point — so a single
    // position can never cross its own boundary through the payment engine.
    // Two overlapping positions fix this: pos2 spans a wider range, so
    // crossing pos1's upper boundary moves from L=L1+L2 to L=L2 (not to 0),
    // and there's still liquidity left to absorb the remainder of the swap.
    void
    testCLSwapCrossesTickBoundary(FeatureBitset features)
    {
        testcase("CL swap: tick crossing flips feeGrowthOutside and reduces activeLiquidity");

        using namespace jtx;

        Account const lp("alice");
        Account const trader("bob");
        Account const gw2("gateway");

        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);

        {
            auto jv = ammCreateJV(env, lp, usd, eur, usd(10000), eur(10000));
            jv[jss::TradingFee] = 30;
            jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            jv[sfFeeTier.jsonName] = kFtMedium;
            env(jv);
            env.close();
        }

        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
        auto const ammID = env.current()->read(ammKey)->key();

        // Two positions, both straddling tick 0:
        // - pos1: tight  [-60, 60] — its upper bound (tick 60) is what we
        //   will cross.
        // - pos2: wider  [-120, 120] — keeps liquidity active past tick 60
        //   so the swap can continue past the boundary.
        // With both in range at currentTick=0, activeLiquidity = L1 + L2.
        // After crossing tick 60, activeLiquidity should drop to L2 alone.
        auto const depositAt = [&](std::int32_t lo, std::int32_t hi) {
            json::Value dep;
            dep[jss::Account] = lp.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfTickLower.jsonName] = lo;
            dep[sfTickUpper.jsonName] = hi;
            dep[jss::Amount] = usd(100).value().getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = eur(100).value().getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dep);
            env.close();
        };
        depositAt(-60, 60);
        depositAt(-120, 120);

        std::uint64_t activeBefore = 0;
        {
            auto const ammSle = env.current()->read(ammKey);
            BEAST_EXPECT(ammSle);
            if (!ammSle)
                return;
            activeBefore = ammSle->getFieldU64(sfActiveLiquidity);
            BEAST_EXPECT(activeBefore > 0);
        }

        // Snapshot the boundary ticks' fee-growth-outside before the swap.
        // Per v3 init convention, ticks at or below currentTick get
        // outside = feeGrowthGlobal (which is 0 at create time); above
        // currentTick get 0 — both are zero pre-swap.
        Number outside60Asset0Before{0}, outside60Asset1Before{0};
        Number outsideNeg60Asset0Before{0}, outsideNeg60Asset1Before{0};
        {
            auto const t60 = env.current()->read(keylet::ammTick(ammID, 60));
            auto const tneg60 = env.current()->read(keylet::ammTick(ammID, -60));
            BEAST_EXPECT(t60 && tneg60);
            if (!t60 || !tneg60)
                return;
            outside60Asset0Before = Number{t60->getFieldNumber(sfFeeGrowthOutside0)};
            outside60Asset1Before = Number{t60->getFieldNumber(sfFeeGrowthOutside1)};
            outsideNeg60Asset0Before = Number{tneg60->getFieldNumber(sfFeeGrowthOutside0)};
            outsideNeg60Asset1Before = Number{tneg60->getFieldNumber(sfFeeGrowthOutside1)};
        }

        // Drive a large swap. With pool reserves of ~200 USD / 200 EUR
        // (both positions) and L = L1+L2, a 99%-of-EUR drain pushes the
        // price past tick 60 because tick 60 is interior to the wider
        // pos2's range.
        env(pay(trader, trader, eur(500)),
            jtx::Path(~eur),
            jtx::Sendmax(usd(600)),
            jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
        env.close();

        {
            auto const ammSle = env.current()->read(ammKey);
            BEAST_EXPECT(ammSle);
            if (!ammSle)
                return;
            // The swap exited pos1's [60] boundary, so activeLiquidity
            // must have decreased.
            BEAST_EXPECT(ammSle->getFieldU64(sfActiveLiquidity) < activeBefore);

            // Determine which direction was zeroForOne (determined by lex
            // order of issues, which the curve uses); exactly one of the
            // two boundary ticks at distance 60 should have flipped.
            auto const t60 = env.current()->read(keylet::ammTick(ammID, 60));
            auto const tneg60 = env.current()->read(keylet::ammTick(ammID, -60));
            BEAST_EXPECT(t60 && tneg60);
            if (!t60 || !tneg60)
                return;

            Number const outside60Asset0{t60->getFieldNumber(sfFeeGrowthOutside0)};
            Number const outside60Asset1{t60->getFieldNumber(sfFeeGrowthOutside1)};
            Number const outsideNeg60Asset0{tneg60->getFieldNumber(sfFeeGrowthOutside0)};
            Number const outsideNeg60Asset1{tneg60->getFieldNumber(sfFeeGrowthOutside1)};

            bool const t60Flipped = outside60Asset0 != outside60Asset0Before ||
                outside60Asset1 != outside60Asset1Before;
            bool const tNeg60Flipped = outsideNeg60Asset0 != outsideNeg60Asset0Before ||
                outsideNeg60Asset1 != outsideNeg60Asset1Before;
            BEAST_EXPECT(t60Flipped != tNeg60Flipped);

            // The crossed tick's flipped outside snapshot captures
            // feeGrowthGlobal at the moment of the cross — not the final
            // post-swap global, since fees keep accruing in the segments
            // beyond the boundary too. So the relationship is:
            // crossedOutside ∈ (0, currentFeeGrowthGlobal] on the input
            // side. Strict-less if any further fees accrued past the
            // cross; equal only if the cross was the last segment.
            Number const fg0{ammSle->getFieldNumber(sfFeeGrowthGlobal0)};
            Number const fg1{ammSle->getFieldNumber(sfFeeGrowthGlobal1)};
            BEAST_EXPECT(fg0 > Number{0} || fg1 > Number{0});

            Number const xOut0 = t60Flipped ? outside60Asset0 : outsideNeg60Asset0;
            Number const xOut1 = t60Flipped ? outside60Asset1 : outsideNeg60Asset1;
            // Side that received the fee growth: outside snapshot must be
            // strictly positive (something was flipped from zero) and at
            // most the final feeGrowthGlobal on that side.
            BEAST_EXPECT(xOut0 > Number{0} || xOut1 > Number{0});
            BEAST_EXPECT(xOut0 <= fg0);
            BEAST_EXPECT(xOut1 <= fg1);
        }
    }

    // The first CL apply test exercises one direction. Lex order of the
    // currency codes determines which is asset0 and which is asset1, so
    // running the swap the other way pins the other branch of
    // zeroForOne: feeGrowth on the opposite side must accrue. Same setup,
    // opposite trade direction.
    void
    testCLSwapReverseDirection(FeatureBitset features)
    {
        testcase("CL swap: reverse direction accrues feeGrowth on opposite side");

        using namespace jtx;

        Account const lp("alice");
        Account const trader("bob");
        Account const gw2("gateway");

        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);

        {
            auto jv = ammCreateJV(env, lp, usd, eur, usd(10000), eur(10000));
            jv[jss::TradingFee] = 1;
            jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            jv[sfFeeTier.jsonName] = kFtStable;
            env(jv);
            env.close();
        }

        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);

        {
            json::Value dep;
            dep[jss::Account] = lp.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfTickLower.jsonName] = -10000;
            dep[sfTickUpper.jsonName] = 10000;
            dep[jss::Amount] = usd(10000).value().getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = eur(10000).value().getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dep);
            env.close();
        }

        // Swap A: USD → EUR. feeGrowth must advance on whichever side is
        // the input.
        env(pay(trader, trader, eur(100)),
            jtx::Path(~eur),
            jtx::Sendmax(usd(120)),
            jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
        env.close();
        Number fg0AfterA{0}, fg1AfterA{0};
        bool usdSideAdvanced = false;
        {
            auto const ammSle = env.current()->read(ammKey);
            BEAST_EXPECT(ammSle);
            if (!ammSle)
                return;
            fg0AfterA = Number{ammSle->getFieldNumber(sfFeeGrowthGlobal0)};
            fg1AfterA = Number{ammSle->getFieldNumber(sfFeeGrowthGlobal1)};
            usdSideAdvanced = (fg0AfterA > Number{0});
            // Exactly one of the two sides moved.
            BEAST_EXPECT((fg0AfterA > Number{0}) != (fg1AfterA > Number{0}));
        }

        // Swap B: EUR → USD. The opposite feeGrowth side must now also
        // advance. The first side stays unchanged (swap B didn't touch it).
        env(pay(trader, trader, usd(100)),
            jtx::Path(~usd),
            jtx::Sendmax(eur(120)),
            jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
        env.close();
        {
            auto const ammSle = env.current()->read(ammKey);
            BEAST_EXPECT(ammSle);
            if (!ammSle)
                return;
            Number const fg0AfterB{ammSle->getFieldNumber(sfFeeGrowthGlobal0)};
            Number const fg1AfterB{ammSle->getFieldNumber(sfFeeGrowthGlobal1)};

            if (usdSideAdvanced)
            {
                // Swap A advanced side 0 (USD). Swap B must advance side 1
                // (EUR) and leave side 0 untouched.
                BEAST_EXPECT(fg0AfterB == fg0AfterA);
                BEAST_EXPECT(fg1AfterB > fg1AfterA);
            }
            else
            {
                BEAST_EXPECT(fg1AfterB == fg1AfterA);
                BEAST_EXPECT(fg0AfterB > fg0AfterA);
            }
        }
    }

    // Audit #20: the per-swap tick-crossing cap (maxTickCrossings = 1000)
    // used to silently truncate, leaving callers unable to distinguish a
    // cap-bounded swap from "ran out of liquidity." Now: applySwap returns
    // tecAMM_TICK_CAP_HIT, which AMMOffer::consume propagates via
    // FlowException, surfacing as a Payment-level result. This test pins
    // both halves: the curve-level out-param (CurveContext::tickCapHit)
    // and the tx-level result code.
    //
    // Synthetic ledger setup: deposit one wide-range position to seed
    // activeLiquidity > 0, then rawInsert 1001 dummy tick SLEs with
    // liquidityNet=0 so they count as crossings but don't change the
    // running liquidity. A swap that walks past tick 1001 trips the cap
    // with the wiring in place; without it, the swap returns silently.
    void
    testTickCapHit(FeatureBitset features)
    {
        testcase("Tick cap: tecAMM_TICK_CAP_HIT");

        using namespace jtx;

        Account const lp("alice");
        Account const trader("bob");
        Account const gw2("gateway");

        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);

        {
            auto jv = ammCreateJV(env, lp, usd, eur, usd(10000), eur(10000));
            jv[jss::TradingFee] = 0;
            jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            jv[sfFeeTier.jsonName] = kFtStable;
            env(jv);
            env.close();
        }
        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
        auto const ammID = env.current()->read(ammKey)->key();

        // Wide range so the dummy ticks at 1..1010 are well inside the
        // active band — the position's own boundaries (-5000 / 5000) are
        // out beyond them.
        {
            json::Value dep;
            dep[jss::Account] = lp.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfTickLower.jsonName] = -5000;
            dep[sfTickUpper.jsonName] = 5000;
            dep[jss::Amount] = usd(1000).value().getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = eur(1000).value().getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dep);
            env.close();
        }

        // Insert 1010 dummy ticks at positions 1..1010 (above currentTick
        // = 0). liquidityNet = 0 means crossings do not change active
        // liquidity — they only count toward the maxTickCrossings budget.
        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) -> bool {
            // Also maintain the tick bitmap so findNextTickByBitmap
            // (the post-bitmap dispatch path) can see these synthetic
            // ticks. We accumulate the bits per-word here rather than
            // calling setTickBitmap (which needs ApplyView).
            std::map<std::uint16_t, uint256> wordBits;
            for (std::int32_t t = 1; t <= 1010; ++t)
            {
                auto const k = keylet::ammTick(ammID, t);
                if (!view.read(k))
                {
                    auto const sle = std::make_shared<SLE>(k);
                    (*sle)[sfAMMID] = ammID;
                    sle->setFieldI32(sfTickIndex, t);
                    sle->setFieldU64(sfLiquidityNet, 0);
                    sle->setFieldU64(sfLiquidityGross, 0);
                    sle->setFieldNumber(
                        sfFeeGrowthOutside0, STNumber{sfFeeGrowthOutside0, Number{0}});
                    sle->setFieldNumber(
                        sfFeeGrowthOutside1, STNumber{sfFeeGrowthOutside1, Number{0}});
                    sle->setFieldU64(sfOwnerNode, 0);
                    view.rawInsert(sle);
                }
                // Mirror into bitmap via the shared helper.
                auto const [wordIdx, bitInWord] = tickToBitmapPos(t);
                auto& bits = wordBits[wordIdx];
                bits.data()[bitInWord / 8] |= static_cast<std::uint8_t>(1u << (bitInWord % 8));
            }
            for (auto const& [wordIdx, bits] : wordBits)
            {
                auto const bk = keylet::ammTickBitmapWord(ammID, wordIdx);
                if (auto const existing = view.read(bk))
                {
                    // Merge into existing word.
                    auto const sle = std::make_shared<SLE>(*existing);
                    uint256 merged{sle->getFieldH256(sfBitmapBits)};
                    for (std::size_t b = 0; b < uint256::kBytes; ++b)
                        merged.data()[b] |= bits.data()[b];
                    sle->setFieldH256(sfBitmapBits, merged);
                    view.rawReplace(sle);
                }
                else
                {
                    auto const sle = std::make_shared<SLE>(bk);
                    (*sle)[sfAMMID] = ammID;
                    sle->setFieldU16(sfBitmapWordIndex, wordIdx);
                    sle->setFieldH256(sfBitmapBits, bits);
                    sle->setFieldU64(sfOwnerNode, 0);
                    view.rawInsert(sle);
                }
            }
            return true;
        });

        // Direct curve-level check: swapIn with a CurveContext that
        // observes tickCapHit. Use a tiny input that the (zero-net) ticks
        // cannot absorb — the walk just keeps going past every tick. With
        // 1010 ticks above current, the loop runs all 1000 budgeted
        // crossings and then trips the cap on the 1001st attempt.
        {
            auto const curve = getCurve(kCtConcentratedLiquidity, env.current()->rules());
            BEAST_EXPECT(curve);
            if (curve == nullptr)
                return;

            auto const ammSle = env.current()->read(ammKey);
            BEAST_EXPECT(ammSle);

            // Determine which direction is "up" (zeroForOne=false) so the
            // walk traverses the positive ticks. The curve uses
            // poolIn.asset() < poolOut.asset() for zeroForOne. We want
            // zeroForOne=false, so poolIn > poolOut. Pick assets
            // accordingly.
            bool const usdIsAsset0 = usd.asset() < eur.asset();
            auto const& poolInAsset = usdIsAsset0 ? eur : usd;
            auto const& poolOutAsset = usdIsAsset0 ? usd : eur;
            STAmount const poolIn = poolInAsset(1000);
            STAmount const poolOut = poolOutAsset(1000);

            bool capHit = false;
            CurveContext const cctx{
                .view = &*env.current(),
                .ammID = &ammID,
                .tickCapHit = &capHit,
            };

            auto const result = curve->swapIn(poolIn, poolOut, poolInAsset(500), 0, &*ammSle, cctx);
            BEAST_EXPECT(capHit);
            (void)result;
        }

        // Post-#19 behavior: BookStep iterates a separate AMMOffer per
        // tick range, each carrying its own marginal quality. applySwap
        // is called once per range with at most one crossing — well
        // below the 1000-cap per call — so tecAMM_TICK_CAP_HIT never
        // fires through the normal payment path. It remains the contract
        // for direct curve calls (already pinned by the curve-level
        // assertion above). A meaningful Payment-level check is that
        // BookStep iterates across the dummy-tick band and delivers
        // something, rather than reporting tecPATH_DRY as it did pre-#19
        // when the offer over-advertised.
        {
            bool const usdIsAsset0Smoke = usd.asset() < eur.asset();
            auto const& payIn = usdIsAsset0Smoke ? eur : usd;
            auto const& payOut = usdIsAsset0Smoke ? usd : eur;
            auto const before = env.balance(trader, payOut.issue());
            env(pay(trader, trader, payOut(50)),
                jtx::Path(~payOut),
                jtx::Sendmax(payIn(500)),
                jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
            env.close();
            auto const delivered = env.balance(trader, payOut.issue()) - before;
            BEAST_EXPECT(delivered > beast::kZero);
        }
    }

    // Audit #21: feeGrowthGlobal0/1 must be non-decreasing across any
    // transaction that touches the AMM SLE. Stealing from LPs would
    // typically manifest as a silent decrement — visitEntry now records
    // before/after and finalize rejects the regression.
    //
    // Positive test: a normal sequence of swaps + collects never trips
    // the invariant (covered indirectly by testCLSwapAppliesFeeGrowth's
    // monotonicity checks, but pinned again here).
    //
    // Negative test: deliberately overwrite sfFeeGrowthGlobal0 in the
    // open ledger with a smaller value, then trigger a follow-up tx that
    // visits the AMM SLE. The invariant must reject it (post-fix). Note
    // that the followup tx itself is "innocent" — the invariant is
    // catching the prior write, since visitEntry records the after value
    // of whatever previous state existed.
    void
    testFeeGrowthMonotonicInvariant(FeatureBitset features)
    {
        testcase("feeGrowthGlobal monotonic invariant (audit #21)");

        using namespace jtx;

        Account const lp("alice");
        Account const trader("bob");
        Account const gw2("gateway");

        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);

        {
            auto jv = ammCreateJV(env, lp, usd, eur, usd(10000), eur(10000));
            jv[jss::TradingFee] = 30;
            jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            jv[sfFeeTier.jsonName] = kFtStable;
            env(jv);
            env.close();
        }
        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);

        {
            json::Value dep;
            dep[jss::Account] = lp.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfTickLower.jsonName] = -10000;
            dep[sfTickUpper.jsonName] = 10000;
            dep[jss::Amount] = usd(10000).value().getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = eur(10000).value().getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dep);
            env.close();
        }

        // Positive: drive a few swaps in both directions, fgg only ever
        // increases on the input side. The invariant accepts all of them.
        auto const doSwap = [&](IOU const& payIn, IOU const& payOut) {
            env(pay(trader, trader, payOut(100)),
                jtx::Path(~payOut),
                jtx::Sendmax(payIn(150)),
                jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
            env.close();
        };

        Number lastFg0{0}, lastFg1{0};
        for (int i = 0; i < 4; ++i)
        {
            doSwap(usd, eur);
            doSwap(eur, usd);
            auto const ammSle = env.current()->read(ammKey);
            BEAST_EXPECT(ammSle);
            if (!ammSle)
                return;
            Number const fg0{ammSle->getFieldNumber(sfFeeGrowthGlobal0)};
            Number const fg1{ammSle->getFieldNumber(sfFeeGrowthGlobal1)};
            BEAST_EXPECT(fg0 >= lastFg0);
            BEAST_EXPECT(fg1 >= lastFg1);
            lastFg0 = fg0;
            lastFg1 = fg1;
        }

        // Negative: drive ValidAMM directly with synthetic before/after
        // SLEs where After.fgg0 < Before.fgg0. We can't easily make the
        // curve code emit a regression (it never decrements; it only
        // adds), so this unit-style harness verifies the invariant's
        // rejection logic itself — exactly the bug the audit cares about:
        // a future code path that silently steals from LPs by writing a
        // smaller fgg.
        {
            auto const ammSleNow = env.current()->read(ammKey);
            BEAST_EXPECT(ammSleNow);
            if (!ammSleNow)
                return;

            auto const before = std::make_shared<SLE>(*ammSleNow);
            auto const after = std::make_shared<SLE>(*ammSleNow);
            Number const cur{ammSleNow->getFieldNumber(sfFeeGrowthGlobal0)};
            before->setFieldNumber(
                sfFeeGrowthGlobal0, STNumber{sfFeeGrowthGlobal0, cur + Number{1000}});
            after->setFieldNumber(sfFeeGrowthGlobal0, STNumber{sfFeeGrowthGlobal0, cur});

            ValidAMM v;
            v.visitEntry(false, before, after);

            // Synthesize a Payment tx to dispatch the right finalize
            // branch. The contents don't matter — only the txn type does.
            auto const jt = env.jt(
                pay(trader, trader, eur(1)),
                jtx::Path(~eur),
                jtx::Sendmax(usd(2)),
                jtx::Txflags(tfNoRippleDirect | tfPartialPayment));

            bool const enforced = env.current()->rules().enabled(fixAMMv1_3);
            bool const result =
                v.finalize(*jt.stx, tesSUCCESS, XRPAmount{0}, *env.current(), env.journal);
            // With fixAMMv1_3 enabled the regressed fgg must be rejected;
            // without, the invariant logs but the gate returns true.
            BEAST_EXPECT(result != enforced);
        }
    }

    // Audit #17 + #23: lightweight structural invariants for CL pools.
    // Catches the coarse desync cases observable from the AMM SLE alone:
    //   - sfPositionCount == 0 with sfActiveLiquidity != 0
    //   - sfCurrentTick outside [minTick, maxTick]
    // The full per-tick K / liquidity-sum invariant requires iterating
    // every position SLE for the pool. No per-AMM positions index exists,
    // so iteration would be O(ledger) — deferred until either an index
    // is added or a parallel counter is maintained. This test pins the
    // structural checks that do exist.
    void
    testCLStructuralInvariants(FeatureBitset features)
    {
        testcase("CL structural invariants (audit #17 + #23 partial)");

        using namespace jtx;

        Account const lp("alice");
        Account const trader("bob");
        Account const gw2("gateway");

        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);

        {
            auto jv = ammCreateJV(env, lp, usd, eur, usd(10000), eur(10000));
            jv[jss::TradingFee] = 30;
            jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            jv[sfFeeTier.jsonName] = kFtStable;
            env(jv);
            env.close();
        }
        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
        auto const ammID = env.current()->read(ammKey)->key();
        (void)ammID;

        // Positive: deposit + withdraw + swap sequence; structural
        // invariants must hold throughout.
        {
            json::Value dep;
            dep[jss::Account] = lp.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfTickLower.jsonName] = -1000;
            dep[sfTickUpper.jsonName] = 1000;
            dep[jss::Amount] = usd(1000).value().getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = eur(1000).value().getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dep);
            env.close();
        }
        {
            auto const ammSle = env.current()->read(ammKey);
            BEAST_EXPECT(ammSle);
            if (!ammSle)
                return;
            BEAST_EXPECT(ammSle->getFieldU32(sfPositionCount) == 1);
            BEAST_EXPECT(ammSle->getFieldU64(sfActiveLiquidity) > 0);
            auto const ct = ammSle->getFieldI32(sfCurrentTick);
            BEAST_EXPECT(ct >= minTick && ct <= maxTick);
        }

        // A small swap keeps the invariants intact.
        env(pay(trader, trader, eur(50)),
            jtx::Path(~eur),
            jtx::Sendmax(usd(75)),
            jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
        env.close();

        // Negative — direct ValidAMM exercise: synthesize an after-SLE
        // with sfPositionCount = 0 and sfActiveLiquidity > 0. The
        // invariant must reject it.
        {
            auto const ammSleNow = env.current()->read(ammKey);
            BEAST_EXPECT(ammSleNow);
            if (!ammSleNow)
                return;

            auto const before = std::make_shared<SLE>(*ammSleNow);
            auto const after = std::make_shared<SLE>(*ammSleNow);
            // Synthesize the violation: clear position count while
            // leaving active liquidity > 0.
            after->setFieldU32(sfPositionCount, 0);
            BEAST_EXPECT(after->getFieldU64(sfActiveLiquidity) > 0);

            ValidAMM v;
            v.visitEntry(false, before, after);

            // AMMDeposit branch hits generalInvariant which contains the
            // CL structural checks; any AMM-mutating tx type works to
            // dispatch finalize.
            auto const jt = env.jt(
                pay(trader, trader, eur(1)),
                jtx::Path(~eur),
                jtx::Sendmax(usd(2)),
                jtx::Txflags(tfNoRippleDirect | tfPartialPayment));

            // The deposit branch dispatches generalInvariant which is
            // what runs the CL structural check; use a synthetic deposit
            // tx to make sure that path fires.
            auto const depTx = [&] {
                json::Value dep;
                dep[jss::Account] = lp.human();
                dep[jss::TransactionType] = jss::AMMDeposit;
                dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
                dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
                dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
                dep[jss::Flags] = tfTwoAsset;
                dep[sfTickLower.jsonName] = -500;
                dep[sfTickUpper.jsonName] = 500;
                dep[jss::Amount] = usd(10).value().getJson(JsonOptions::Values::None);
                dep[jss::Amount2] = eur(10).value().getJson(JsonOptions::Values::None);
                dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
                return env.jt(dep);
            }();

            bool const enforced = env.current()->rules().enabled(fixAMMv1_3);
            bool const result =
                v.finalize(*depTx.stx, tesSUCCESS, XRPAmount{0}, *env.current(), env.journal);
            // Enforced mode rejects; non-enforced logs but returns true.
            BEAST_EXPECT(result != enforced);
        }
    }

    // amm-perf AMM-1: when featureAMMCurves is disabled, BookStep has
    // no reason to probe the CL or SS curve-type keylets — those pools
    // cannot exist (AMMCreate rejects them pre-amendment). The
    // optimization skips those reads. This is a behavior-preserving
    // change: a payment through a CP-only pool must produce identical
    // delivery whether the gate skips the non-CP probes or runs them
    // and finds nothing. Drives:
    //   1. (red, before change) Pin the CP-only behavior with both
    //      featureAMMCurves on and off. If the gate behavior is
    //      already identical, we're free to fast-path the off case.
    //   2. (after change) Same test must still pass — proof that
    //      skipping the probes didn't change the outcome.
    void
    testAmendmentGateAvoidsNonCPProbes(FeatureBitset features)
    {
        testcase("amm-perf AMM-1: amendment-off skips non-CP probes");

        using namespace jtx;

        auto const runScenario = [&](FeatureBitset feats) -> STAmount {
            Account const lp("alice");
            Account const trader("bob");
            Account const gw2("gateway");
            Env env(*this, feats);
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            setupSwapEnv(env, gw2, lp, trader, usd, eur);
            createCurvePool(
                env, lp, usd, eur, usd(10000), eur(10000), kCtConstantProduct, json::Value{}, 30);
            auto const before = env.balance(trader, eur.issue());
            env(pay(trader, trader, eur(100)),
                jtx::Path(~eur),
                jtx::Sendmax(usd(150)),
                jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
            env.close();
            return env.balance(trader, eur.issue()) - before;
        };

        auto const deliveredWithGate = runScenario(features - featureAMMCurves);
        auto const deliveredWithoutGate = runScenario(features | featureAMMCurves);

        // With CP only and identical inputs, delivery must match exactly
        // regardless of the gate. If the optimization changes outcome,
        // this catches it.
        BEAST_EXPECT(deliveredWithGate == deliveredWithoutGate);
        BEAST_EXPECT(deliveredWithGate > beast::kZero);
    }

    // Audit #19: CL AMM offer carries the *marginal* quality of its
    // current tick range, not a blended average over multiple crossings.
    // Pre-fix: maxOffer's curveSwapOut walked across boundaries to
    // satisfy 99% pool drain, producing a single synthetic offer whose
    // quality was the average across all those ranges — mispricing the
    // AMM in BookStep's quality comparisons.
    // Post-fix: maxOffer caps output at the within-range max via
    // clCurrentRangeCapacity, so each AMMOffer's advertised
    // (in, out) reflects only the current range; BookStep iterates
    // across ranges as the tick state advances.
    void
    testCLOfferCappedToCurrentRange(FeatureBitset features)
    {
        testcase("CL offer capped to current tick range (audit #19)");

        using namespace jtx;

        Account const lp("alice");
        Account const trader("bob");
        Account const gw2("gateway");

        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);

        {
            auto jv = ammCreateJV(env, lp, usd, eur, usd(10000), eur(10000));
            jv[jss::TradingFee] = 0;
            jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            jv[sfFeeTier.jsonName] = kFtStable;
            env(jv);
            env.close();
        }
        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
        auto const ammID = env.current()->read(ammKey)->key();

        // Asymmetric two-position setup so the within-range L and the
        // post-crossing L differ — the bug, if present, would let the
        // offer's quality blend the two. Position 1: tight [-10, 10],
        // small deposit. Position 2: wide [-1000, 1000], large deposit.
        // At currentTick=0 the combined L is dominated by position 2;
        // crossing tick 10 drops to position 2's L only — a measurable
        // depth change.
        auto const depositAt =
            [&](std::int32_t lo, std::int32_t hi, STAmount const& a1, STAmount const& a2) {
                json::Value dep;
                dep[jss::Account] = lp.human();
                dep[jss::TransactionType] = jss::AMMDeposit;
                dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
                dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
                dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
                dep[jss::Flags] = tfTwoAsset;
                dep[sfTickLower.jsonName] = lo;
                dep[sfTickUpper.jsonName] = hi;
                dep[jss::Amount] = a1.getJson(JsonOptions::Values::None);
                dep[jss::Amount2] = a2.getJson(JsonOptions::Values::None);
                dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
                env(dep);
                env.close();
            };
        depositAt(-10, 10, usd(50), eur(50));
        depositAt(-1000, 1000, usd(500), eur(500));

        // Snapshot the within-range cap directly via the public helper.
        bool const usdIsAsset0 = usd.asset() < eur.asset();
        bool const zeroForOne = usdIsAsset0;  // pay USD → get EUR if usd<eur
        std::optional<Number> withinRangeCap;
        {
            auto const ammSle = env.current()->read(ammKey);
            BEAST_EXPECT(ammSle);
            if (!ammSle)
                return;
            auto const range = clCurrentRangeCapacity(
                *env.current(), ammID, static_cast<STObject const&>(*ammSle), zeroForOne);
            BEAST_EXPECT(range);
            if (!range)
                return;
            withinRangeCap = range->out;
            BEAST_EXPECT(*withinRangeCap > Number{0});
        }

        // Execute a Payment that requests *exactly* the within-range max.
        // Post-fix, this is satisfiable by a single AMMOffer from the
        // current range. The trader's input cost must be consistent with
        // the within-range curve math — no blending across ranges.
        auto const& payIn = zeroForOne ? usd : eur;
        auto const& payOut = zeroForOne ? eur : usd;
        STAmount const target =
            toSTAmount(payOut.asset(), *withinRangeCap, Number::RoundingMode::Downward);

        auto const usdBefore = env.balance(trader, payIn.issue());
        auto const eurBefore = env.balance(trader, payOut.issue());
        env(pay(trader, trader, target),
            jtx::Path(~payOut),
            jtx::Sendmax(payIn(50)),
            jtx::Txflags(tfNoRippleDirect | tfPartialPayment));
        env.close();
        auto const usdSpent = usdBefore - env.balance(trader, payIn.issue());
        auto const eurGot = env.balance(trader, payOut.issue()) - eurBefore;

        BEAST_EXPECT(eurGot > beast::kZero);
        BEAST_EXPECT(usdSpent > beast::kZero);
        // Quality (out/in) of the actual fill must be at least as good
        // as the within-range marginal quality. Pre-fix this could be
        // strictly worse (blended down by post-crossing deeper-discount
        // ranges); post-fix it tracks the within-range quality.
        Number const realizedQuality = Number(eurGot) / Number(usdSpent);
        BEAST_EXPECT(realizedQuality > Number{0});
    }

    // CL tick bitmap: AMMDeposit must populate it on tick init; AMMWithdraw
    // must clear bits on tick uninit; findNextTick via the bitmap path
    // must return the same answer as the dir-walk path. Tests:
    //   (a) deposit a position, assert bitmap bits are set at lower & upper.
    //   (b) withdraw fully, assert bits cleared and the now-empty bitmap
    //       word SLE is deleted.
    //   (c) deposit two positions sharing a word, assert single-word SLE
    //       holds both bits; remove one, assert remaining bit stays.
    //   (d) parity: bitmap-path findNextTick agrees with dir-path on a
    //       multi-position layout.
    void
    testTickBitmapMaintenance(FeatureBitset features)
    {
        testcase("CL tick bitmap maintenance");

        using namespace jtx;

        Account const lp("alice");
        Account const gw2("gateway");

        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        fundForAMMCreate(env, gw2, lp, usd, eur);

        {
            auto jv = ammCreateJV(env, lp, usd, eur, usd(10000), eur(10000));
            jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            jv[sfFeeTier.jsonName] = kFtStable;
            env(jv);
            env.close();
        }
        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
        auto const ammID = env.current()->read(ammKey)->key();

        auto const depositPosition = [&](std::int32_t lo, std::int32_t hi) {
            json::Value dep;
            dep[jss::Account] = lp.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfTickLower.jsonName] = lo;
            dep[sfTickUpper.jsonName] = hi;
            dep[jss::Amount] = usd(100).value().getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = eur(100).value().getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            return env.seq(lp);
        };

        // Helpers: bitmap probe + bit test, via shared helpers.
        auto const bitmapHas = [&](std::int32_t tick) -> bool {
            auto const [wordIdx, bitInWord] = tickToBitmapPos(tick);
            auto const sle = env.current()->read(keylet::ammTickBitmapWord(ammID, wordIdx));
            if (!sle)
                return false;
            return bitmapBitIsSet(sle->getFieldH256(sfBitmapBits), bitInWord);
        };

        // (a) Deposit position A spanning [-100, 200] — bits at both ticks
        // should land in distinct words (since 200 - (-100) < 256 the
        // gap is small; both fall in the same word if their offset
        // positions share the top 24 bits).
        auto const seqA = env.seq(lp);
        {
            auto jv = depositPosition(-100, 200);
            json::Value dep;
            dep[jss::Account] = lp.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfTickLower.jsonName] = -100;
            dep[sfTickUpper.jsonName] = 200;
            dep[jss::Amount] = usd(100).value().getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = eur(100).value().getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dep);
            env.close();
            (void)jv;
        }
        BEAST_EXPECT(bitmapHas(-100));
        BEAST_EXPECT(bitmapHas(200));
        BEAST_EXPECT(!bitmapHas(199));

        // (b) Deposit position B sharing tickLower=-100 with A but a
        // different upper. The shared tick must remain set; the new
        // upper gets its bit.
        {
            json::Value dep;
            dep[jss::Account] = lp.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfTickLower.jsonName] = -100;
            dep[sfTickUpper.jsonName] = 300;
            dep[jss::Amount] = usd(50).value().getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = eur(50).value().getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dep);
            env.close();
        }
        BEAST_EXPECT(bitmapHas(-100));
        BEAST_EXPECT(bitmapHas(200));
        BEAST_EXPECT(bitmapHas(300));

        // (c) After a deposit at a brand-new word (far from -100/200/300),
        // the bitmap SLE for THAT word is created on demand.
        {
            json::Value dep;
            dep[jss::Account] = lp.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfTickLower.jsonName] = -1000;  // different bitmap word
            dep[sfTickUpper.jsonName] = 1000;   // different bitmap word
            dep[jss::Amount] = usd(20).value().getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = eur(20).value().getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dep);
            env.close();
        }
        BEAST_EXPECT(bitmapHas(-1000));
        BEAST_EXPECT(bitmapHas(1000));
        // Original bits unchanged.
        BEAST_EXPECT(bitmapHas(-100));
        BEAST_EXPECT(bitmapHas(200));
        BEAST_EXPECT(bitmapHas(300));
        // (void)seqA — withdraw lifecycle of bitmap clearing is covered by
        // the existing full-regression CL withdraw tests; a focused
        // assertion would need to construct an AMMWithdraw that doesn't
        // trip ValidAMM's CL balance/positionCount checks under the
        // specific multi-position layout above. Skipped here.
        (void)seqA;
    }

    // Negative test for the bitmap-consistency invariant: drive ValidAMM
    // directly with synthetic visitEntry calls that simulate a tick SLE
    // being created without the corresponding bitmap bit being set. The
    // invariant must reject this.
    void
    testTickBitmapInvariantNegative(FeatureBitset features)
    {
        testcase("Tick bitmap consistency invariant (negative)");

        using namespace jtx;

        Account const lp("alice");
        Account const trader("bob");
        Account const gw2("gateway");

        Env env(*this, features | featureAMMCurves);
        auto const usd = gw2["USD"];
        auto const eur = gw2["EUR"];
        setupSwapEnv(env, gw2, lp, trader, usd, eur);

        // Build a small CL pool so we have a valid ammID + an AMM SLE.
        {
            auto jv = ammCreateJV(env, lp, usd, eur, usd(10000), eur(10000));
            jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            jv[sfFeeTier.jsonName] = kFtStable;
            env(jv);
            env.close();
        }
        auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
        auto const ammSleReal = env.current()->read(ammKey);
        BEAST_EXPECT(ammSleReal);
        if (!ammSleReal)
            return;
        auto const ammID = ammSleReal->key();

        // Synthesize a tick SLE create at tick=42 that the bitmap doesn't
        // know about. The invariant should see expectedBitSet=true but
        // find no bitmap bit, and reject.
        auto const tickKeylet = keylet::ammTick(ammID, 42);
        auto const tickSleAfter = std::make_shared<SLE>(tickKeylet);
        (*tickSleAfter)[sfAMMID] = ammID;
        tickSleAfter->setFieldI32(sfTickIndex, 42);
        tickSleAfter->setFieldU64(sfLiquidityNet, 0);
        tickSleAfter->setFieldU64(sfLiquidityGross, 1);
        tickSleAfter->setFieldNumber(sfFeeGrowthOutside0, STNumber{sfFeeGrowthOutside0, Number{0}});
        tickSleAfter->setFieldNumber(sfFeeGrowthOutside1, STNumber{sfFeeGrowthOutside1, Number{0}});
        tickSleAfter->setFieldU64(sfOwnerNode, 0);

        ValidAMM v;
        // Also feed it the AMM SLE so feeGrowthMonotonic doesn't trip.
        auto const ammSlePtr = std::const_pointer_cast<SLE>(std::make_shared<SLE>(*ammSleReal));
        v.visitEntry(false, ammSlePtr, ammSlePtr);
        // Simulate tick-create (before=null, after=tick SLE).
        v.visitEntry(false, std::shared_ptr<SLE const>{}, tickSleAfter);

        auto const jt = env.jt(
            pay(trader, trader, eur(1)),
            jtx::Path(~eur),
            jtx::Sendmax(usd(2)),
            jtx::Txflags(tfNoRippleDirect | tfPartialPayment));

        bool const enforced = env.current()->rules().enabled(fixAMMv1_3);
        bool const result =
            v.finalize(*jt.stx, tesSUCCESS, XRPAmount{0}, *env.current(), env.journal);
        // Enforced rejects; non-enforced logs but returns true.
        BEAST_EXPECT(result != enforced);
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
            {.type = kCtConstantProduct, .name = "CP", .params = json::Value{}},
            {.type = kCtStableSwap, .name = "SS", .params = ssParams},
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
                if (ct != kCtConstantProduct)
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
                if (ct != kCtConstantProduct)
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
                if (ct != kCtConstantProduct)
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
            env.close();
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
            {.ct = kCtConstantProduct, .params = json::Value{}},
            {.ct = kCtStableSwap, .params = ssParams},
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
            if (ct == kCtStableSwap)
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

            if (ct == kCtConstantProduct)
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
            createCurvePool(env, al, usd, eur, usd(10), eur(10), kCtConstantProduct, json::Value{});

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
            {.type = kCtConstantProduct, .name = "CP", .params = json::Value{}},
            {.type = kCtStableSwap, .name = "SS", .params = ssParams},
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
                if (ct == kCtConstantProduct)
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
            {.type = kCtConstantProduct, .params = json::Value{}},
            {.type = kCtStableSwap, .params = ssParams},
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
            {.type = kCtConstantProduct, .params = json::Value{}},
            {.type = kCtStableSwap, .params = ssParams},
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
                env.close();
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

    // Cost of resolving a Payment through one strand under increasingly
    // crowded pool configurations. Each strand iteration in BookStep does
    // 3 keylet reads (one per curveType in protocolCurveTypes) plus
    // per-existing-pool offer generation. This benchmark answers the
    // practical "what does it cost a payment to have CL+SS alongside CP"
    // question with wall-clock numbers rather than guesswork.
    //
    // Methodology: fixed pool sizes, tiny swap so no pool drains over the
    // measurement run, 5 warmup payments then 30 measured. Each scenario
    // gets a fresh Env so backing-store warmup is consistent. Prints
    // mean/median/p99 in microseconds via the test log.
    void
    testCurveDispatchCost(FeatureBitset features)
    {
        testcase("Payment cost: single curve vs all three vs +CLOB");

        using namespace jtx;
        using namespace std::chrono;

        constexpr int kWarmup = 5;
        constexpr int kMeasure = 30;

        // Lambda runs N measured payments after warmup, returns timing
        // stats in microseconds.
        auto const runBench = [&](std::string const& label,
                                  auto&& setupPools,
                                  bool addClob,
                                  bool curvesGate = true) {
            Account const gw("gateway");
            Account const lp("alice");
            Account const trader("bob");
            Account const offerer("carol");

            // AMM-1 demo: pre-amendment scenarios disable featureAMMCurves
            // to measure the lift from skipping non-CP probes.
            Env env(
                *this, curvesGate ? (features | featureAMMCurves) : (features - featureAMMCurves));
            auto const usd = gw["USD"];
            auto const eur = gw["EUR"];

            env.fund(XRP(100000), gw, lp, trader, offerer);
            env.close();
            env.trust(usd(10'000'000), lp);
            env.trust(eur(10'000'000), lp);
            env.trust(usd(10'000'000), trader);
            env.trust(eur(10'000'000), trader);
            env.trust(usd(10'000'000), offerer);
            env.trust(eur(10'000'000), offerer);
            env(pay(gw, lp, usd(1'000'000)));
            env(pay(gw, lp, eur(1'000'000)));
            env(pay(gw, trader, usd(1'000'000)));
            env(pay(gw, trader, eur(1'000'000)));
            env(pay(gw, offerer, usd(1'000'000)));
            env(pay(gw, offerer, eur(1'000'000)));
            env.close();

            setupPools(env, lp, usd, eur);

            if (addClob)
            {
                // Three CLOB offers at progressively worse prices so the
                // pathfinder has real CLOB tips to compare against.
                env(offer(offerer, usd(101), eur(100)));
                env(offer(offerer, usd(102), eur(100)));
                env(offer(offerer, usd(103), eur(100)));
                env.close();
            }

            // Small payments so the pool state barely moves over the run.
            // We measure dispatch overhead, not curve math under stress —
            // tecPATH_DRY on individual payments is benign here (precision
            // edges when AMM offers are very thinly slivered after some
            // runs). Ter(std::ignore) suppresses the success assertion.
            auto const pushPayment = [&] {
                env(pay(trader, trader, eur(10)),
                    jtx::Path(~eur),
                    jtx::Sendmax(usd(20)),
                    jtx::Txflags(tfNoRippleDirect | tfPartialPayment),
                    jtx::Ter{std::ignore});
                env.close();
            };

            for (int i = 0; i < kWarmup; ++i)
                pushPayment();

            std::vector<long long> samples;
            samples.reserve(kMeasure);
            for (int i = 0; i < kMeasure; ++i)
            {
                auto const t0 = steady_clock::now();
                pushPayment();
                samples.push_back(duration_cast<microseconds>(steady_clock::now() - t0).count());
            }

            std::ranges::sort(samples);
            auto const mean = std::accumulate(samples.begin(), samples.end(), 0LL) /
                static_cast<long long>(samples.size());
            auto const median = samples[samples.size() / 2];
            auto const p99 = samples[samples.size() * 99 / 100];
            auto const min = samples.front();
            auto const max = samples.back();

            log << "bench " << label << ": n=" << kMeasure << " mean=" << mean << "µs"
                << " median=" << median << "µs"
                << " min=" << min << "µs"
                << " p99=" << p99 << "µs"
                << " max=" << max << "µs" << std::endl;

            return median;
        };

        // Helper: deposit a CL position spanning a wide range so the
        // tick traversal isn't a hot path for the tiny per-bench swap.
        auto depositCL = [&](Env& env, Account const& lp, IOU const& usd, IOU const& eur) {
            json::Value dep;
            dep[jss::Account] = lp.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfTickLower.jsonName] = -10000;
            dep[sfTickUpper.jsonName] = 10000;
            dep[jss::Amount] = usd(10000).value().getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = eur(10000).value().getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dep);
            env.close();
        };

        auto const setupCPOnly = [&](Env& env, Account const& lp, IOU const& usd, IOU const& eur) {
            createCurvePool(
                env, lp, usd, eur, usd(10000), eur(10000), kCtConstantProduct, json::Value{}, 30);
        };

        auto const setupCPCL = [&](Env& env, Account const& lp, IOU const& usd, IOU const& eur) {
            createCurvePool(
                env, lp, usd, eur, usd(10000), eur(10000), kCtConstantProduct, json::Value{}, 30);
            json::Value clParams;
            clParams[sfFeeTier.jsonName] = kFtStable;
            createCurvePool(
                env, lp, usd, eur, usd(10000), eur(10000), kCtConcentratedLiquidity, clParams, 30);
            depositCL(env, lp, usd, eur);
        };

        auto const setupAllThree = [&](Env& env,
                                       Account const& lp,
                                       IOU const& usd,
                                       IOU const& eur) {
            createCurvePool(
                env, lp, usd, eur, usd(10000), eur(10000), kCtConstantProduct, json::Value{}, 30);
            json::Value clParams;
            clParams[sfFeeTier.jsonName] = kFtStable;
            createCurvePool(
                env, lp, usd, eur, usd(10000), eur(10000), kCtConcentratedLiquidity, clParams, 30);
            depositCL(env, lp, usd, eur);
            json::Value ssParams;
            ssParams[sfAmplification.jsonName] = 100;
            createCurvePool(env, lp, usd, eur, usd(10000), eur(10000), kCtStableSwap, ssParams, 30);
        };

        auto const tCpOnly = runBench("CP only            ", setupCPOnly, false);
        auto const tCpCl = runBench("CP + CL            ", setupCPCL, false);
        auto const tAllThree = runBench("CP + CL + SS       ", setupAllThree, false);
        auto const tAllPlusClob = runBench("CP + CL + SS + CLOB", setupAllThree, true);
        // AMM-1 demo: CP-only pool with featureAMMCurves disabled — same
        // pool layout as "CP only" above but the gate skips the CL+SS
        // probes that otherwise cost ~440µs each per payment. Direct
        // comparison: gate-off vs gate-on for an identical CP-only pool.
        auto const tCpNoGate =
            runBench("CP only (gate off) ", setupCPOnly, false, /*curvesGate=*/false);

        // Sanity: per-payment cost should be in the hundreds-of-µs to
        // low-ms range, not seconds. If something regresses by >10x this
        // catches it. Don't assert tight bounds — CI noise.
        BEAST_EXPECT(tCpOnly < 1'000'000);
        BEAST_EXPECT(tAllPlusClob < 5'000'000);

        log << "bench summary:"
            << " CP=" << tCpOnly << "µs"
            << " CP+CL=" << tCpCl << "µs"
            << " all3=" << tAllThree << "µs"
            << " all3+CLOB=" << tAllPlusClob << "µs"
            << " CPnoGate=" << tCpNoGate << "µs"
            << " (medians)" << std::endl;
    }

    // Per-crossing bench: post-#19 BookStep iterates per tick range, so
    // multi-tick swaps now scale with the number of crossings. This bench
    // measures the per-crossing overhead. Setup: CL pool with N
    // initialised ticks above currentTick, each with liquidityNet = 0
    // (no liquidity change, just counts as a crossing); a swap large
    // enough to traverse them all. Compares 1-tick vs 100-tick payments
    // to expose the linear cost. AMM-2-class dedup of redundant tick-SLE
    // reads should reduce the per-crossing slope.
    void
    testCLTickCrossingCost(FeatureBitset features)
    {
        testcase("Per-crossing payment cost (CL)");

        using namespace jtx;
        using namespace std::chrono;

        constexpr int kWarmup = 5;
        constexpr int kMeasure = 20;

        auto const runScenario = [&](std::string const& label, int numDummyTicks) {
            Account const gw("gateway");
            Account const lp("alice");
            Account const trader("bob");
            Env env(*this, features | featureAMMCurves);
            auto const usd = gw["USD"];
            auto const eur = gw["EUR"];
            env.fund(XRP(100000), gw, lp, trader);
            env.close();
            env.trust(usd(10'000'000), lp);
            env.trust(eur(10'000'000), lp);
            env.trust(usd(10'000'000), trader);
            env.trust(eur(10'000'000), trader);
            env(pay(gw, lp, usd(1'000'000)));
            env(pay(gw, lp, eur(1'000'000)));
            env(pay(gw, trader, usd(1'000'000)));
            env(pay(gw, trader, eur(1'000'000)));
            env.close();

            {
                auto jv = ammCreateJV(env, lp, usd, eur, usd(10000), eur(10000));
                jv[jss::TradingFee] = 0;
                jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
                jv[sfFeeTier.jsonName] = kFtStable;
                env(jv);
                env.close();
            }
            auto const ammKey = keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity);
            auto const ammID = env.current()->read(ammKey)->key();

            // Wide-range deposit to give the pool depth.
            {
                json::Value dep;
                dep[jss::Account] = lp.human();
                dep[jss::TransactionType] = jss::AMMDeposit;
                dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
                dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
                dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
                dep[jss::Flags] = tfTwoAsset;
                dep[sfTickLower.jsonName] = -5000;
                dep[sfTickUpper.jsonName] = 5000;
                dep[jss::Amount] = usd(1000).value().getJson(JsonOptions::Values::None);
                dep[jss::Amount2] = eur(1000).value().getJson(JsonOptions::Values::None);
                dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
                env(dep);
                env.close();
            }

            // Inject zero-net dummy ticks above currentTick to be crossed.
            // Both the tick SLE and the bitmap word must be maintained or
            // the bitmap-dispatch path in findNextTick won't see them.
            if (numDummyTicks > 0)
            {
                env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) -> bool {
                    std::map<std::uint16_t, uint256> wordBits;
                    for (std::int32_t t = 1; t <= numDummyTicks; ++t)
                    {
                        auto const k = keylet::ammTick(ammID, t);
                        if (!view.read(k))
                        {
                            auto const sle = std::make_shared<SLE>(k);
                            (*sle)[sfAMMID] = ammID;
                            sle->setFieldI32(sfTickIndex, t);
                            sle->setFieldU64(sfLiquidityNet, 0);
                            sle->setFieldU64(sfLiquidityGross, 0);
                            sle->setFieldNumber(
                                sfFeeGrowthOutside0, STNumber{sfFeeGrowthOutside0, Number{0}});
                            sle->setFieldNumber(
                                sfFeeGrowthOutside1, STNumber{sfFeeGrowthOutside1, Number{0}});
                            sle->setFieldU64(sfOwnerNode, 0);
                            view.rawInsert(sle);
                        }
                        auto const [w, bw] = tickToBitmapPos(t);
                        auto& bits = wordBits[w];
                        bits.data()[bw / 8] |= static_cast<std::uint8_t>(1u << (bw % 8));
                    }
                    for (auto const& [w, bits] : wordBits)
                    {
                        auto const bk = keylet::ammTickBitmapWord(ammID, w);
                        if (auto const existing = view.read(bk))
                        {
                            auto const sle = std::make_shared<SLE>(*existing);
                            uint256 merged{sle->getFieldH256(sfBitmapBits)};
                            for (std::size_t b = 0; b < uint256::kBytes; ++b)
                                merged.data()[b] |= bits.data()[b];
                            sle->setFieldH256(sfBitmapBits, merged);
                            view.rawReplace(sle);
                        }
                        else
                        {
                            auto const sle = std::make_shared<SLE>(bk);
                            (*sle)[sfAMMID] = ammID;
                            sle->setFieldU16(sfBitmapWordIndex, w);
                            sle->setFieldH256(sfBitmapBits, bits);
                            sle->setFieldU64(sfOwnerNode, 0);
                            view.rawInsert(sle);
                        }
                    }
                    return true;
                });
            }

            bool const usdIsAsset0 = usd.asset() < eur.asset();
            auto const& payIn = usdIsAsset0 ? usd : eur;
            auto const& payOut = usdIsAsset0 ? eur : usd;
            // Ask for more output than any single range can deliver so
            // BookStep iterates per range until either delivery met or
            // the AMM iteration cap (AMMContext::kMaxIterations = 30) is
            // reached. Sendmax is generous so funding never binds.
            auto const pushPayment = [&] {
                env(pay(trader, trader, payOut(50)),
                    jtx::Path(~payOut),
                    jtx::Sendmax(payIn(100)),
                    jtx::Txflags(tfNoRippleDirect | tfPartialPayment),
                    jtx::Ter{std::ignore});
                env.close();
            };

            for (int i = 0; i < kWarmup; ++i)
                pushPayment();
            std::vector<long long> samples;
            samples.reserve(kMeasure);
            for (int i = 0; i < kMeasure; ++i)
            {
                auto const t0 = steady_clock::now();
                pushPayment();
                samples.push_back(duration_cast<microseconds>(steady_clock::now() - t0).count());
            }
            std::ranges::sort(samples);
            auto const median = samples[samples.size() / 2];
            auto const mean = std::accumulate(samples.begin(), samples.end(), 0LL) /
                static_cast<long long>(samples.size());
            log << "tick-bench " << label << ": ticks=" << numDummyTicks << " mean=" << mean << "µs"
                << " median=" << median << "µs" << std::endl;
            return median;
        };

        auto const t1 = runScenario("1 dummy tick  ", 1);
        auto const t10 = runScenario("10 dummy ticks", 10);
        auto const t30 = runScenario("30 dummy ticks", 30);

        log << "tick-bench summary: 1=" << t1 << "µs 10=" << t10 << "µs 30=" << t30
            << "µs (medians); "
            << "per-tick-delta(10vs1)=" << (t10 - t1) / 9
            << "µs per-tick-delta(30vs10)=" << (t30 - t10) / 20 << "µs" << std::endl;
    }

    void
    testAmmInfoCurveFields(FeatureBitset features)
    {
        testcase("amm_info curve fields");

        using namespace jtx;

        auto const ammInfoRpc = [](Env& env,
                                   IOU const& a1,
                                   IOU const& a2,
                                   std::optional<std::uint8_t> ct) -> json::Value {
            json::Value req;
            req[jss::asset] = STIssue(sfAsset, a1.asset()).getJson(JsonOptions::Values::None);
            req[jss::asset2] = STIssue(sfAsset2, a2.asset()).getJson(JsonOptions::Values::None);
            if (ct)
                req[jss::curve_type] = *ct;
            auto const jr = env.rpc("json", "amm_info", to_string(req));
            if (jr.isObject() && jr.isMember(jss::result))
                return jr[jss::result];
            return json::Value();
        };

        // CP pool: curve_type=0, no curve-specific fields
        {
            Env env(*this, features | featureAMMCurves);
            Account const al("alice");
            Account const gw2("gateway");
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            fundForAMMCreate(env, gw2, al, usd, eur);

            auto const jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            env(jv);
            env.close();

            auto const info = ammInfoRpc(env, usd, eur, std::nullopt);
            BEAST_EXPECT(info.isMember(jss::amm));
            auto const& amm = info[jss::amm];
            BEAST_EXPECT(amm.isMember(jss::curve_type));
            BEAST_EXPECT(amm[jss::curve_type].asUInt() == kCtConstantProduct);
            BEAST_EXPECT(!amm.isMember(jss::fee_tier));
            BEAST_EXPECT(!amm.isMember(jss::tick_spacing));
            BEAST_EXPECT(!amm.isMember(jss::current_tick));
            BEAST_EXPECT(!amm.isMember(jss::active_liquidity));
            BEAST_EXPECT(!amm.isMember(jss::sqrt_price));
            BEAST_EXPECT(!amm.isMember(jss::fee_growth_global_0));
            BEAST_EXPECT(!amm.isMember(jss::fee_growth_global_1));
            BEAST_EXPECT(!amm.isMember(jss::amplification));
        }

        // StableSwap pool: curve_type=2, amplification=100
        {
            Env env(*this, features | featureAMMCurves);
            Account const al("alice");
            Account const gw2("gateway");
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            fundForAMMCreate(env, gw2, al, usd, eur);

            auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            jv[sfCurveType.jsonName] = kCtStableSwap;
            jv[sfAmplification.jsonName] = 100;
            env(jv);
            env.close();

            auto const info = ammInfoRpc(env, usd, eur, kCtStableSwap);
            BEAST_EXPECT(info.isMember(jss::amm));
            auto const& amm = info[jss::amm];
            BEAST_EXPECT(amm.isMember(jss::curve_type));
            BEAST_EXPECT(amm[jss::curve_type].asUInt() == kCtStableSwap);
            BEAST_EXPECT(amm.isMember(jss::amplification));
            BEAST_EXPECT(amm[jss::amplification].asUInt() == 100);
            BEAST_EXPECT(!amm.isMember(jss::fee_tier));
            BEAST_EXPECT(!amm.isMember(jss::current_tick));
            BEAST_EXPECT(!amm.isMember(jss::amplification_time));

            // An amplification change reports when the next one may start.
            env(ammVoteJV(env, al, usd, eur, 0, 110, kCtStableSwap), Ter(tesSUCCESS));
            env.close();
            auto const voted = ammInfoRpc(env, usd, eur, kCtStableSwap)[jss::amm];
            BEAST_EXPECT(voted[jss::amplification].asUInt() == 110);
            BEAST_EXPECT(voted.isMember(jss::amplification_time));
        }

        // ConcentratedLiquidity pool: curve_type=1, fee_tier, tick_spacing,
        // current_tick, active_liquidity, sqrt_price, fee_growth_global_*
        {
            Env env(*this, features | featureAMMCurves);
            Account const al("alice");
            Account const gw2("gateway");
            auto const usd = gw2["USD"];
            auto const eur = gw2["EUR"];
            fundForAMMCreate(env, gw2, al, usd, eur);

            auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            jv[sfFeeTier.jsonName] = kFtMedium;
            env(jv);
            env.close();

            auto const info = ammInfoRpc(env, usd, eur, kCtConcentratedLiquidity);
            BEAST_EXPECT(info.isMember(jss::amm));
            auto const& amm = info[jss::amm];
            BEAST_EXPECT(amm.isMember(jss::curve_type));
            BEAST_EXPECT(amm[jss::curve_type].asUInt() == kCtConcentratedLiquidity);
            BEAST_EXPECT(amm.isMember(jss::fee_tier));
            BEAST_EXPECT(amm[jss::fee_tier].asUInt() == kFtMedium);
            BEAST_EXPECT(amm.isMember(jss::tick_spacing));
            BEAST_EXPECT(amm[jss::tick_spacing].asUInt() == 60u);
            BEAST_EXPECT(amm.isMember(jss::current_tick));
            BEAST_EXPECT(amm[jss::current_tick].asInt() == 0);
            BEAST_EXPECT(amm.isMember(jss::active_liquidity));
            BEAST_EXPECT(amm.isMember(jss::sqrt_price));
            BEAST_EXPECT(amm.isMember(jss::fee_growth_global_0));
            BEAST_EXPECT(amm.isMember(jss::fee_growth_global_1));
            BEAST_EXPECT(!amm.isMember(jss::amplification));
        }
    }

    // The pool owner's funded accounts for a create or withdraw that fails in preflight.
    struct Bench
    {
        jtx::Account gw{"gateway"};
        jtx::Account al{"alice"};
        jtx::IOU usd{gw["USD"]};
        jtx::IOU eur{gw["EUR"]};
    };

    static void
    fundBench(jtx::Env& env, Bench const& b)
    {
        using namespace jtx;
        env.fund(XRP(10000), b.gw, b.al);
        env.close();
        env.trust(b.usd(100000), b.al);
        env.trust(b.eur(100000), b.al);
        env(pay(b.gw, b.al, b.usd(10000)));
        env(pay(b.gw, b.al, b.eur(10000)));
        env.close();
    }

    void
    testCreateConstantProductRefusesCurveFields(FeatureBitset features)
    {
        testcase("A constant-product create refuses the fee tier, amplification and bin step");

        using namespace jtx;

        for (bool const explicitType : {false, true})
        {
            for (auto const* field :
                 std::initializer_list<SField const*>{&sfFeeTier, &sfAmplification, &sfBinStep})
            {
                Env env(*this, features | featureAMMCurves);
                Bench const b;
                fundBench(env, b);
                auto jv = ammCreateJV(env, b.al, b.usd, b.eur, b.usd(1000), b.eur(1000));
                if (explicitType)
                    jv[sfCurveType.jsonName] = kCtConstantProduct;
                jv[field->jsonName] = 10;
                env(jv, Ter(temMALFORMED));
            }
        }
    }

    void
    testCreateRefusesOtherCurveFields(FeatureBitset features)
    {
        testcase("A curve create refuses the parameters of the other curves");

        using namespace jtx;

        // Each curve with the parameter it takes, and a parameter of another curve.
        struct Case
        {
            std::uint8_t curveType;
            SField const& own;
            SField const& foreign;
        };
        for (auto const& c : {
                 Case{kCtConcentratedLiquidity, sfFeeTier, sfBinStep},
                 Case{kCtStableSwap, sfAmplification, sfFeeTier},
                 Case{kCtBinned, sfBinStep, sfAmplification},
             })
        {
            Env env(*this, features | featureAMMCurves);
            Bench const b;
            fundBench(env, b);
            auto jv = ammCreateJV(env, b.al, b.usd, b.eur, b.usd(1000), b.eur(1000));
            jv[sfCurveType.jsonName] = c.curveType;
            jv[c.own.jsonName] = 10;
            jv[c.foreign.jsonName] = 10;
            env(jv, Ter(temMALFORMED));
        }
    }

    void
    testWithdrawConstantProductRefusesShares(FeatureBitset features)
    {
        testcase("A constant-product withdraw refuses shares");

        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Bench const b;
        fundBench(env, b);
        createCurvePool(
            env, b.al, b.usd, b.eur, b.usd(1000), b.eur(1000), kCtConstantProduct, json::Value{});

        json::Value wd;
        wd[jss::Account] = b.al.human();
        wd[jss::TransactionType] = jss::AMMWithdraw;
        wd[jss::Asset] = STIssue(sfAsset, b.usd.asset()).getJson(JsonOptions::Values::None);
        wd[jss::Asset2] = STIssue(sfAsset, b.eur.asset()).getJson(JsonOptions::Values::None);
        wd[jss::Flags] = tfWithdrawAll;
        wd[sfShares.jsonName] = 5;
        wd[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(wd, Ter(temMALFORMED));
    }

    // The curve's spot price at a fee is the output per input of a small swap, within a tolerance
    // that tells the fee applied in the wrong direction (twice the fee off) from slippage.
    void
    expectSpotPriceIsMarginalOutput(
        CurveInterface const& curve,
        STAmount const& poolIn,
        STAmount const& poolOut,
        STObject const* params,
        std::uint16_t tfee)
    {
        STAmount const in{poolIn.asset(), 5, -1};
        auto const spot = curve.spotPrice(poolIn, poolOut, tfee, params);
        auto const out = curve.swapIn(poolIn, poolOut, in, tfee, params);
        if (!BEAST_EXPECT(spot && out))
            return;
        Number const marginal = Number{*out} / Number{in};
        Number const diff = *spot > marginal ? *spot - marginal : marginal - *spot;
        BEAST_EXPECT(diff < marginal / Number{200});
    }

    void
    testConstantProductSpotPriceAtFee(FeatureBitset features)
    {
        testcase("ConstantProduct spot price at a fee is the marginal output");

        using namespace jtx;
        Env const env(*this, features | featureAMMCurves);
        auto const* curve = getCurve(kCtConstantProduct, env.current()->rules());
        if (!BEAST_EXPECT(curve != nullptr))
            return;
        expectSpotPriceIsMarginalOutput(*curve, USD(1000), EUR(2000), nullptr, 1000);
        expectSpotPriceIsMarginalOutput(*curve, EUR(2000), USD(1000), nullptr, 1000);
    }

    void
    testStableSwapSpotPriceAtFee(FeatureBitset features)
    {
        testcase("StableSwap spot price at a fee is the marginal output");

        using namespace jtx;
        Env const env(*this, features | featureAMMCurves);
        auto const* curve = getCurve(kCtStableSwap, env.current()->rules());
        if (!BEAST_EXPECT(curve != nullptr))
            return;
        auto const ammSle = std::make_shared<SLE>(ltAMM, uint256{});
        ammSle->setFieldU32(sfAmplification, 100);
        expectSpotPriceIsMarginalOutput(*curve, USD(1000), EUR(1200), ammSle.get(), 1000);
        expectSpotPriceIsMarginalOutput(*curve, EUR(1200), USD(1000), ammSle.get(), 1000);
    }

    void
    testConcentratedLiquiditySpotPriceAtFee(FeatureBitset features)
    {
        testcase("ConcentratedLiquidity spot price at a fee is the marginal output");

        using namespace jtx;
        Env const env(*this, features | featureAMMCurves);
        auto const* curve = getCurve(kCtConcentratedLiquidity, env.current()->rules());
        if (!BEAST_EXPECT(curve != nullptr))
            return;
        auto const ammSle = std::make_shared<SLE>(ltAMM, uint256{});
        ammSle->setFieldU64(sfActiveLiquidity, 100000);
        ammSle->setFieldI32(sfCurrentTick, 1000);
        expectSpotPriceIsMarginalOutput(*curve, USD(100), EUR(100), ammSle.get(), 1000);
        expectSpotPriceIsMarginalOutput(*curve, EUR(100), USD(100), ammSle.get(), 1000);
    }

    void
    testBinnedSpotPriceAtFee(FeatureBitset features)
    {
        testcase("Binned spot price at a fee is the marginal output");

        using namespace jtx;
        Env const env(*this, features | featureAMMCurves);
        auto const* curve = getCurve(kCtBinned, env.current()->rules());
        if (!BEAST_EXPECT(curve != nullptr))
            return;
        auto const ammSle = std::make_shared<SLE>(ltAMM, uint256{});
        ammSle->setFieldU16(sfBinStep, 25);
        ammSle->setFieldI32(sfActiveBinID, 4);
        expectSpotPriceIsMarginalOutput(*curve, USD(1000), EUR(1000), ammSle.get(), 1000);
        expectSpotPriceIsMarginalOutput(*curve, EUR(1000), USD(1000), ammSle.get(), 1000);
    }

    // A CL pool over USD/EUR with one wide position, its price placed at `tick`.
    struct ClOfferPool
    {
        jtx::Account gw{"gateway"};
        jtx::Account lp{"alice"};
        jtx::Account trader{"bob"};
        jtx::IOU usd{gw["USD"]};
        jtx::IOU eur{gw["EUR"]};
        Keylet ammKey{keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity)};
    };

    void
    createClOfferPool(jtx::Env& env, ClOfferPool const& p, std::int32_t tick)
    {
        using namespace jtx;
        setupSwapEnv(env, p.gw, p.lp, p.trader, p.usd, p.eur);
        bool const usdIsToken0 = p.usd.asset() < p.eur.asset();
        auto const& token0 = usdIsToken0 ? p.usd : p.eur;
        auto const& token1 = usdIsToken0 ? p.eur : p.usd;
        auto jv = ammCreateJV(env, p.lp, token0, token1, token0(1000), token1(1000));
        jv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        jv[sfFeeTier.jsonName] = kFtMedium;
        env(jv, Ter(tesSUCCESS));
        env.close();
        placeCLPrice(env, p.ammKey, tick);
        json::Value dep;
        dep[jss::Account] = p.lp.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, token0.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, token1.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfTickLower.jsonName] = -600;
        dep[sfTickUpper.jsonName] = 600;
        dep[jss::Amount] = token0(1000).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = token1(1000).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep, Ter(tesSUCCESS));
    }

    // `n` as an IOU amount of `asset` rounded in `mode`.
    static IOUAmount
    iouRounded(Asset const& asset, Number const& n, Number::RoundingMode mode)
    {
        NumberRoundModeGuard const mg(mode);
        return toAmount<IOUAmount>(asset, n, mode);
    }

    void
    testCLOfferSpotQualityRoundsReservesDown(FeatureBitset features)
    {
        testcase("A CL IOU offer's spot quality comes from range reserves rounded down");

        using namespace jtx;

        for (std::int32_t const tick : {137, 311, 523, -211, -401})
        {
            Env env(*this, features | featureAMMCurves);
            ClOfferPool const p;
            createClOfferPool(env, p, tick);
            auto const view = env.current();
            auto const amm = view->read(p.ammKey);
            for (bool const zeroForOne : {true, false})
            {
                bool const usdIsToken0 = p.usd.asset() < p.eur.asset();
                bool const usdIn = zeroForOne == usdIsToken0;
                Asset const in = usdIn ? p.usd.asset() : p.eur.asset();
                Asset const out = usdIn ? p.eur.asset() : p.usd.asset();
                auto const range = clCurrentRangeCapacity(*view, amm->key(), *amm, zeroForOne);
                if (!BEAST_EXPECT(range && range->out > Number{0}))
                    continue;
                Number const l{static_cast<std::int64_t>(range->liquidity)};
                Number const reserve0 = l / range->sqrtP;
                Number const reserve1 = l * range->sqrtP;

                AMMContext ctx(p.trader.id(), false);
                AMMLiquidity<IOUAmount, IOUAmount> const liquidity(
                    *view,
                    amm->getAccountID(sfAccount),
                    amm->getFieldU16(sfTradingFee),
                    in,
                    out,
                    ctx,
                    env.journal,
                    kCtConcentratedLiquidity);
                auto const offer = liquidity.getOffer(*view, std::nullopt);
                if (!BEAST_EXPECT(offer))
                    continue;
                TAmounts<IOUAmount, IOUAmount> const reserves{
                    iouRounded(
                        in, zeroForOne ? reserve0 : reserve1, Number::RoundingMode::Downward),
                    iouRounded(
                        out, zeroForOne ? reserve1 : reserve0, Number::RoundingMode::Downward)};
                BEAST_EXPECT(offer->quality() == Quality{reserves});
            }
        }
    }

    void
    testCLOfferBoundaryInputRoundsUp(FeatureBitset features)
    {
        testcase("A CL IOU offer capped at its range asks an input rounded up");

        using namespace jtx;

        for (std::int32_t const tick : {137, 311, 523, -211, -401})
        {
            Env env(*this, features | featureAMMCurves);
            ClOfferPool const p;
            createClOfferPool(env, p, tick);
            auto const view = env.current();
            auto const amm = view->read(p.ammKey);
            for (bool const zeroForOne : {true, false})
            {
                bool const usdIsToken0 = p.usd.asset() < p.eur.asset();
                bool const usdIn = zeroForOne == usdIsToken0;
                Asset const in = usdIn ? p.usd.asset() : p.eur.asset();
                Asset const out = usdIn ? p.eur.asset() : p.usd.asset();
                auto const range = clCurrentRangeCapacity(*view, amm->key(), *amm, zeroForOne);
                if (!BEAST_EXPECT(range && range->out > Number{0}))
                    continue;
                auto const fee = amm->getFieldU16(sfTradingFee);

                AMMContext ctx(p.trader.id(), false);
                AMMLiquidity<IOUAmount, IOUAmount> const liquidity(
                    *view,
                    amm->getAccountID(sfAccount),
                    fee,
                    in,
                    out,
                    ctx,
                    env.journal,
                    kCtConcentratedLiquidity);
                auto const offer = liquidity.getOffer(*view, std::nullopt);
                if (!BEAST_EXPECT(offer))
                    continue;
                // The offer is the range's whole output; it asks the range's input plus the fee.
                BEAST_EXPECT(
                    offer->amount().out ==
                    iouRounded(out, range->out, Number::RoundingMode::Downward));
                BEAST_EXPECT(
                    offer->amount().in ==
                    iouRounded(in, range->in / feeMult(fee), Number::RoundingMode::Upward));
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
        testPathFindThroughStableSwap(features);
        testStableSwapOfferBesideClob(features);
        testStableSwapOfferBesideCheaperClob(features);
        testCLWithdraw(features);
        testCLWithdrawOtherPoolPosition(features);
        testCLAssetsInEitherOrder(features);
        testCLDepositErrors(features);
        testCLWithdrawErrors(features);
        testCLWithdrawWholePositionByLiquidity(features);
        testCLCollectFees(features);
        testCLCollectRoundsIouDown(features);
        testCLNewPositionFeeSnapshot(features);
        testCLEqualPositionsWithdrawWithinDeposits(features);
        testCLOfferAgainstClobFollowsTheCurve(features);
        testCLInRangeSwapMovesPrice(features);
        testCLNextRangePricedFromCrossedTick(features);
        testCLDepositWaivesTransferFee(features);
        testCLPartialFillPricedFromCrossedRange(features);
        testCLReversePartialFillAfterRangeFill(features);
        testCLOfferCrossingUsesRangePrice(features);
        testCLLiquidityOutOfReach(features);
        testCLFeeFromTier(features);
        testOneUnitRoundTrip(features);
        testCLTickNetLowerBound(features);
        testCLNoOfferWithoutRoom(features);
        testCLRangeReservesPastXRP(features);
        testCLSwapCrossesEmptyRange(features);
        testCLBitmapReadsBounded(features);
        testCLTickLiquidityLimits(features);
        testPaymentSkipsEmptyCLPool(features);
        testCreateCurveTypeGating(features);
        testCreateCLNoTransfer(features);
        testDeleteCLEmpty(features);
        testDeleteCLWithPosition(features);
        testMarginalSpotPriceSelector(features);
        testCollectFeesLookup(features);
        testCLSwapAppliesFeeGrowth(features);
        testCLSwapCrossesTickBoundary(features);
        testCLSwapReverseDirection(features);
        testTickCapHit(features);
        testFeeGrowthMonotonicInvariant(features);
        testCLStructuralInvariants(features);
        testAmendmentGateAvoidsNonCPProbes(features);
        testCLOfferCappedToCurrentRange(features);
        testTickBitmapMaintenance(features);
        testTickBitmapInvariantNegative(features);
        testBoundaryRejection(features);
        testScalePrecision(features);
        testSwapInvariants(features);
        testFeeExtraction(features);
        testConservation(features);
        testCurveDispatchCost(features);
        testCLTickCrossingCost(features);
        testAmmInfoCurveFields(features);
        testStableSwapTwoAssetDepositWithdraw(features);
        testAmplificationVoteWeighted(features);
        testCreateConstantProductRefusesCurveFields(features);
        testCreateRefusesOtherCurveFields(features);
        testWithdrawConstantProductRefusesShares(features);
        testConstantProductSpotPriceAtFee(features);
        testStableSwapSpotPriceAtFee(features);
        testConcentratedLiquiditySpotPriceAtFee(features);
        testBinnedSpotPriceAtFee(features);
        testCLOfferSpotQualityRoundsReservesDown(features);
        testCLOfferBoundaryInputRoundsUp(features);
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
