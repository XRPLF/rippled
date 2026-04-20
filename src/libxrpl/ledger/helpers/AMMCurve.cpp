// Concentrated liquidity tick math, swap routing, and fee accounting derived
// from XRPL-Standards Discussion #427 by Roman Thpt (@RomThpt).
// StableSwap (Newton's method) and weighted curve math are original.
// See: https://github.com/XRPLF/XRPL-Standards/discussions/427

#include <xrpl/ledger/helpers/AMMCurve.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/ledger/helpers/AMMTickMath.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>

#include <boost/endian/conversion.hpp>

#include <cstdint>
#include <optional>
#include <utility>

namespace xrpl {

namespace {

// StableSwap Newton's method helpers

Number
computeD(Number const& x, Number const& y, Number const& a)
{
    Number const s = x + y;
    if (s == Number{0})
        return Number{0};
    if (a <= Number{0})
        return Number{0};

    Number const ann = a * 4;  // A * n^n, n=2
    Number d = s;

    for (int i = 0; i < newtonMaxIterations; ++i)
    {
        // D_p = D^3 / (4 * x * y)
        Number const dP = (d * d * d) / (4 * x * y);
        Number const dPrev = d;

        // D = (Ann * S + 2 * D_p) * D / ((Ann - 1) * D + 3 * D_p)
        d = (ann * s + 2 * dP) * d / ((ann - 1) * d + 3 * dP);

        auto const diff = (d > dPrev) ? d - dPrev : dPrev - d;
        if (diff <= Number{1, -15})
            return d;
    }

    return d;
}

Number
computeY(Number const& x, Number const& d, Number const& a)
{
    if (a <= Number{0})
        return Number{0};
    Number const ann = a * 4;
    Number const c = (d * d * d) / (4 * ann * x);
    Number const b = x + d / ann - d;

    Number y = d;

    for (int i = 0; i < newtonMaxIterations; ++i)
    {
        Number const yPrev = y;
        y = (y * y + c) / (2 * y + b);

        auto const diff = (y > yPrev) ? y - yPrev : yPrev - y;
        if (diff <= Number{1, -15})
            return y;
    }

    return y;
}

std::optional<std::int32_t>
findNextTick(ReadView const& view, uint256 const& ammID, std::int32_t currentTick, bool zeroForOne)
{
    auto const tickKey = keylet::ammTick(ammID, currentTick);
    auto const base = keylet::ammTickBase(ammID);
    auto const end = keylet::ammTickEnd(ammID);

    if (zeroForOne)
    {
        auto const prev = view.pred(tickKey.key, base.key);
        if (!prev)
            return std::nullopt;
        if (*prev <= base.key || *prev >= end.key)
            return std::nullopt;
    }
    else
    {
        auto const next = view.succ(tickKey.key, end.key);
        if (!next)
            return std::nullopt;
        if (*next <= base.key || *next >= end.key)
            return std::nullopt;
    }

    // Decode tick index from the key
    auto decodeTickFromKey = [&](uint256 const& key) -> std::int32_t {
        static constexpr std::int64_t tickOffset = 887272;
        auto const encoded = boost::endian::big_to_native(((std::uint64_t const*)key.end())[-1]);
        return static_cast<std::int32_t>(static_cast<std::int64_t>(encoded) - tickOffset);
    };

    if (zeroForOne)
    {
        auto const prev = view.pred(tickKey.key, base.key);
        return decodeTickFromKey(*prev);
    }

    auto const next = view.succ(tickKey.key, end.key);
    return decodeTickFromKey(*next);
}

//-----------------------------------------------------------------------
// CurveType 0: ConstantProduct
//-----------------------------------------------------------------------
class ConstantProductCurve final : public CurveInterface
{
public:
    Expected<STAmount, TER>
    swapIn(
        STAmount const& poolIn,
        STAmount const& poolOut,
        STAmount const& assetIn,
        std::uint16_t tfee,
        STObject const*,
        CurveContext const& = {}) const override
    {
        auto const f = feeMult(tfee);
        Number const num = poolIn * poolOut;
        Number const denom = poolIn + Number(assetIn) * f;

        if (denom <= Number{0})
            return Unexpected(tecAMM_FAILED);

        Number const out = poolOut - num / denom;
        if (out <= Number{0})
            return Unexpected(tecAMM_FAILED);

        NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
        return toSTAmount(poolOut.asset(), out);
    }

    Expected<STAmount, TER>
    swapOut(
        STAmount const& poolIn,
        STAmount const& poolOut,
        STAmount const& assetOut,
        std::uint16_t tfee,
        STObject const*,
        CurveContext const& = {}) const override
    {
        auto const f = feeMult(tfee);
        Number const denom = poolOut - Number(assetOut);

        if (denom <= Number{0})
            return Unexpected(tecAMM_FAILED);

        Number const in = (poolIn * poolOut / denom - poolIn) / f;
        if (in <= Number{0})
            return Unexpected(tecAMM_FAILED);

        NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
        return toSTAmount(poolIn.asset(), in);
    }

    Expected<Number, TER>
    spotPrice(
        STAmount const& poolIn,
        STAmount const& poolOut,
        std::uint16_t tfee,
        STObject const*,
        CurveContext const& = {}) const override
    {
        auto const f = feeMult(tfee);
        return Number(poolOut) / Number(poolIn) / f;
    }

    [[nodiscard]] TER
    validateParams(STObject const&) const override
    {
        return tesSUCCESS;
    }

    Expected<STAmount, TER>
    initialLPTokens(
        STAmount const& asset1,
        STAmount const& asset2,
        Issue const& lptIssue,
        STObject const*) const override
    {
        return ammLPTokens(asset1, asset2, lptIssue);
    }

    bool
    checkInvariant(
        STAmount const& oldIn,
        STAmount const& oldOut,
        STAmount const& newIn,
        STAmount const& newOut,
        STObject const*) const override
    {
        Number const oldProduct = Number(oldIn) * Number(oldOut);
        Number const newProduct = Number(newIn) * Number(newOut);
        return newProduct >= oldProduct ||
            withinRelativeDistance(oldProduct, newProduct, Number{1, -7});
    }
};

//-----------------------------------------------------------------------
// CurveType 1: ConcentratedLiquidity
//-----------------------------------------------------------------------
class ConcentratedLiquidityCurve final : public CurveInterface
{
    static std::pair<Number, Number>
    singleRangeSwapIn(
        Number const& l,
        Number const& sqrtP,
        Number const& feeAdjustedIn,
        bool zeroForOne)
    {
        Number sqrtPriceNext;
        Number out;
        if (zeroForOne)
        {
            sqrtPriceNext = (l * sqrtP) / (l + feeAdjustedIn * sqrtP);
            out = l * (sqrtP - sqrtPriceNext);
        }
        else
        {
            sqrtPriceNext = sqrtP + feeAdjustedIn / l;
            out = l * (sqrtPriceNext - sqrtP) / (sqrtP * sqrtPriceNext);
        }
        return {sqrtPriceNext, out};
    }

    static Number
    inputToTickBoundary(
        Number const& L,
        Number const& sqrtP,
        Number const& sqrtPTarget,
        bool zeroForOne)
    {
        if (zeroForOne)
        {
            return L * (sqrtP - sqrtPTarget) / (sqrtP * sqrtPTarget);
        }
        return L * (sqrtPTarget - sqrtP);
    }

    static Number
    outputAtTickBoundary(
        Number const& L,
        Number const& sqrtP,
        Number const& sqrtPTarget,
        bool zeroForOne)
    {
        if (zeroForOne)
        {
            return L * (sqrtP - sqrtPTarget);
        }
        return L * (sqrtPTarget - sqrtP) / (sqrtP * sqrtPTarget);
    }

    static std::pair<Number, Number>
    singleRangeSwapOut(
        Number const& l,
        Number const& sqrtP,
        Number const& desiredOut,
        bool zeroForOne)
    {
        Number sqrtPriceNext;
        Number in;
        if (zeroForOne)
        {
            sqrtPriceNext = sqrtP - desiredOut / l;
            if (sqrtPriceNext <= Number{0})
                return {Number{0}, Number{-1}};
            in = l * (sqrtP - sqrtPriceNext) / (sqrtP * sqrtPriceNext);
        }
        else
        {
            Number const denom = l - desiredOut * sqrtP;
            if (denom <= Number{0})
                return {Number{0}, Number{-1}};
            sqrtPriceNext = (l * sqrtP) / denom;
            in = l * (sqrtPriceNext - sqrtP);
        }
        return {sqrtPriceNext, in};
    }

public:
    Expected<STAmount, TER>
    swapIn(
        STAmount const& poolIn,
        STAmount const& poolOut,
        STAmount const& assetIn,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& cctx = {}) const override
    {
        if (ammSle == nullptr)
            return Unexpected(tecINTERNAL);

        auto activeLiquidity = ammSle->getFieldU64(sfActiveLiquidity);
        if (activeLiquidity == 0)
            return Unexpected(tecAMM_FAILED);

        auto const f = feeMult(tfee);
        bool const zeroForOne = poolIn.asset() < poolOut.asset();
        Number const feeAdjustedIn = Number(assetIn) * f;

        auto currentTick = ammSle->getFieldI32(sfCurrentTick);

        Number l(activeLiquidity);
        Number sqrtP = tickToSqrtPrice(currentTick);

        if ((cctx.view == nullptr) || (cctx.ammID == nullptr))
        {
            auto const [_, out] = singleRangeSwapIn(l, sqrtP, feeAdjustedIn, zeroForOne);
            if (out <= Number{0})
                return Unexpected(tecAMM_FAILED);
            NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
            return toSTAmount(poolOut.asset(), out);
        }

        Number remainingIn = feeAdjustedIn;
        Number totalOut{0};
        int maxCrosses = 100;

        while (remainingIn > Number{0} && maxCrosses-- > 0)
        {
            auto const nextTick = findNextTick(*cctx.view, *cctx.ammID, currentTick, zeroForOne);
            if (!nextTick)
                break;

            Number const sqrtPTarget = tickToSqrtPrice(*nextTick);
            Number const toTarget = inputToTickBoundary(l, sqrtP, sqrtPTarget, zeroForOne);

            if (toTarget < Number{0} || remainingIn <= toTarget)
            {
                auto const [_, out] = singleRangeSwapIn(l, sqrtP, remainingIn, zeroForOne);
                totalOut = totalOut + out;
                remainingIn = Number{0};
                break;
            }

            totalOut = totalOut + outputAtTickBoundary(l, sqrtP, sqrtPTarget, zeroForOne);
            remainingIn = remainingIn - toTarget;
            sqrtP = sqrtPTarget;

            auto const tickSle = cctx.view->read(keylet::ammTick(*cctx.ammID, *nextTick));
            if (!tickSle)
                break;

            auto const netRaw = static_cast<std::int64_t>(tickSle->getFieldU64(sfLiquidityNet));
            auto const newLiq =
                static_cast<std::int64_t>(activeLiquidity) + (zeroForOne ? -netRaw : netRaw);

            if (newLiq <= 0)
                break;

            activeLiquidity = static_cast<std::uint64_t>(newLiq);

            l = Number(activeLiquidity);
            currentTick = *nextTick;
        }

        if (totalOut <= Number{0})
            return Unexpected(tecAMM_FAILED);

        NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
        return toSTAmount(poolOut.asset(), totalOut);
    }

    Expected<STAmount, TER>
    swapOut(
        STAmount const& poolIn,
        STAmount const& poolOut,
        STAmount const& assetOut,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& cctx = {}) const override
    {
        if (ammSle == nullptr)
            return Unexpected(tecINTERNAL);

        auto activeLiquidity = ammSle->getFieldU64(sfActiveLiquidity);
        if (activeLiquidity == 0)
            return Unexpected(tecAMM_FAILED);

        auto const f = feeMult(tfee);
        bool const zeroForOne = poolIn.asset() < poolOut.asset();

        auto currentTick = ammSle->getFieldI32(sfCurrentTick);

        Number l(activeLiquidity);
        Number sqrtP = tickToSqrtPrice(currentTick);

        if ((cctx.view == nullptr) || (cctx.ammID == nullptr))
        {
            auto const [_, in_] = singleRangeSwapOut(l, sqrtP, Number(assetOut), zeroForOne);
            if (in_ <= Number{0})
                return Unexpected(tecAMM_FAILED);
            NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
            return toSTAmount(poolIn.asset(), in_ / f);
        }

        Number remainingOut = Number(assetOut);
        Number totalIn{0};
        int maxCrosses = 100;

        while (remainingOut > Number{0} && maxCrosses-- > 0)
        {
            auto const nextTick = findNextTick(*cctx.view, *cctx.ammID, currentTick, zeroForOne);
            if (!nextTick)
                break;

            Number const sqrtPTarget = tickToSqrtPrice(*nextTick);
            Number const maxOut = outputAtTickBoundary(l, sqrtP, sqrtPTarget, zeroForOne);

            if (maxOut < Number{0} || remainingOut <= maxOut)
            {
                auto const [_, in_] = singleRangeSwapOut(l, sqrtP, remainingOut, zeroForOne);
                if (in_ <= Number{0})
                    return Unexpected(tecAMM_FAILED);
                totalIn = totalIn + in_;
                remainingOut = Number{0};
                break;
            }

            totalIn = totalIn + inputToTickBoundary(l, sqrtP, sqrtPTarget, zeroForOne);
            remainingOut = remainingOut - maxOut;
            sqrtP = sqrtPTarget;

            auto const tickSle = cctx.view->read(keylet::ammTick(*cctx.ammID, *nextTick));
            if (!tickSle)
                break;

            auto const netRaw = static_cast<std::int64_t>(tickSle->getFieldU64(sfLiquidityNet));
            auto const newLiq =
                static_cast<std::int64_t>(activeLiquidity) + (zeroForOne ? -netRaw : netRaw);

            if (newLiq <= 0)
                break;

            activeLiquidity = static_cast<std::uint64_t>(newLiq);
            l = Number(activeLiquidity);
            currentTick = *nextTick;
        }

        if (totalIn <= Number{0})
            return Unexpected(tecAMM_FAILED);

        totalIn = totalIn / f;

        NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
        return toSTAmount(poolIn.asset(), totalIn);
    }

    Expected<Number, TER>
    spotPrice(
        STAmount const& poolIn,
        STAmount const& poolOut,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& = {}) const override
    {
        if (ammSle == nullptr)
            return Unexpected(tecINTERNAL);

        auto const tick = ammSle->getFieldI32(sfCurrentTick);
        Number const sqrtP = tickToSqrtPrice(tick);
        Number const price = sqrtP * sqrtP;
        auto const f = feeMult(tfee);

        bool const zeroForOne = poolIn.asset() < poolOut.asset();
        return zeroForOne ? price / f : (Number{1} / price) / f;
    }

    [[nodiscard]] TER
    validateParams(STObject const& curveParams) const override
    {
        if (!curveParams.isFieldPresent(sfFeeTier))
            return temMALFORMED;

        auto const feeTier = curveParams.getFieldU8(sfFeeTier);
        if (feeTier >= feeTierCount)
            return temMALFORMED;

        return tesSUCCESS;
    }

    Expected<STAmount, TER>
    initialLPTokens(
        STAmount const& asset1,
        STAmount const& asset2,
        Issue const& lptIssue,
        STObject const*) const override
    {
        return ammLPTokens(asset1, asset2, lptIssue);
    }

    bool
    checkInvariant(
        STAmount const&,
        STAmount const&,
        STAmount const&,
        STAmount const&,
        STObject const*) const override
    {
        return true;
    }
};

//-----------------------------------------------------------------------
// CurveType 2: StableSwap
//-----------------------------------------------------------------------
class StableSwapCurve final : public CurveInterface
{
public:
    Expected<STAmount, TER>
    swapIn(
        STAmount const& poolIn,
        STAmount const& poolOut,
        STAmount const& assetIn,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& = {}) const override
    {
        if (ammSle == nullptr)
            return Unexpected(tecINTERNAL);

        auto const a = Number(ammSle->getFieldU32(sfAmplification));
        auto const f = feeMult(tfee);

        Number const x = poolIn;
        Number const y = poolOut;
        Number const dx = Number(assetIn) * f;

        Number const d = computeD(x, y, a);
        Number const newX = x + dx;
        Number const newY = computeY(newX, d, a);
        Number const dy = y - newY;

        if (dy <= Number{0} || dy >= y)
            return Unexpected(tecAMM_FAILED);

        NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
        return toSTAmount(poolOut.asset(), dy);
    }

    Expected<STAmount, TER>
    swapOut(
        STAmount const& poolIn,
        STAmount const& poolOut,
        STAmount const& assetOut,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& = {}) const override
    {
        if (ammSle == nullptr)
            return Unexpected(tecINTERNAL);

        auto const a = Number(ammSle->getFieldU32(sfAmplification));
        auto const f = feeMult(tfee);

        Number const x = poolIn;
        Number const y = poolOut;
        Number const newY = y - Number(assetOut);

        if (newY <= Number{0})
            return Unexpected(tecAMM_FAILED);

        Number const d = computeD(x, y, a);
        Number const newX = computeY(newY, d, a);
        Number const dx = (newX - x) / f;

        if (dx <= Number{0})
            return Unexpected(tecAMM_FAILED);

        NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
        return toSTAmount(poolIn.asset(), dx);
    }

    Expected<Number, TER>
    spotPrice(
        STAmount const& poolIn,
        STAmount const& poolOut,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& = {}) const override
    {
        if (ammSle == nullptr)
            return Unexpected(tecINTERNAL);

        auto const a = Number(ammSle->getFieldU32(sfAmplification));
        Number const x = poolIn;
        Number const y = poolOut;
        Number const d = computeD(x, y, a);
        Number const ann = a * 4;
        Number const d3 = d * d * d;

        Number const num = ann + d3 / (4 * x * x * y);
        Number const den = ann + d3 / (4 * x * y * y);
        auto const f = feeMult(tfee);

        return (num / den) / f;
    }

    [[nodiscard]] TER
    validateParams(STObject const& curveParams) const override
    {
        if (!curveParams.isFieldPresent(sfAmplification))
            return temMALFORMED;

        auto const amp = curveParams.getFieldU32(sfAmplification);
        if (amp < minAmplification || amp > maxAmplification)
            return temMALFORMED;

        return tesSUCCESS;
    }

    Expected<STAmount, TER>
    initialLPTokens(
        STAmount const& asset1,
        STAmount const& asset2,
        Issue const& lptIssue,
        STObject const* curveParams) const override
    {
        if (curveParams == nullptr)
            return Unexpected(tecINTERNAL);

        auto const a = Number(curveParams->getFieldU32(sfAmplification));
        auto const d = computeD(Number(asset1), Number(asset2), a);
        return toSTAmount(lptIssue, d);
    }

    bool
    checkInvariant(
        STAmount const& oldIn,
        STAmount const& oldOut,
        STAmount const& newIn,
        STAmount const& newOut,
        STObject const* ammSle) const override
    {
        if (ammSle == nullptr)
            return false;
        auto const a = Number(ammSle->getFieldU32(sfAmplification));
        Number const oldD = computeD(Number(oldIn), Number(oldOut), a);
        Number const newD = computeD(Number(newIn), Number(newOut), a);
        return newD >= oldD || withinRelativeDistance(oldD, newD, Number{1, -7});
    }
};

// Singletons
ConstantProductCurve const kConstantProductCurve;
ConcentratedLiquidityCurve const kConcentratedLiquidityCurve;
StableSwapCurve const kStableSwapCurve;

}  // namespace

CurveInterface const*
getCurve(std::uint8_t curveType, Rules const& rules)
{
    switch (curveType)
    {
        case CtConstantProduct:
            return &kConstantProductCurve;

        case CtConcentratedLiquidity:
            if (rules.enabled(featureAMMCurves))
                return &kConcentratedLiquidityCurve;
            return nullptr;

        case CtStableSwap:
            if (rules.enabled(featureAMMCurves))
                return &kStableSwapCurve;
            return nullptr;

        default:
            return nullptr;
    }
}

}  // namespace xrpl
