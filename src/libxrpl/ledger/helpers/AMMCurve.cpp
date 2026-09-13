// Concentrated liquidity tick math, swap routing, and fee accounting derived
// from XRPL-Standards Discussion #427 by Roman Thpt (@RomThpt).
// StableSwap (Newton's method) and weighted curve math are original.
// See: https://github.com/XRPLF/XRPL-Standards/discussions/427

#include <xrpl/ledger/helpers/AMMCurve.h>

#include <expected>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>

#include <boost/endian/conversion.hpp>
#include <xrpl/ledger/ApplyView.h>
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
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>

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

// Apply a tick's liquidityNet to the running activeLiquidity using only signed
// arithmetic with explicit range checks. liquidityNet is stored as raw UINT64
// bits but interpreted as int64 (Uniswap v3 convention — added at upper tick,
// subtracted at lower tick on initialise). Returns std::nullopt only on
// overflow or a strictly-negative result; 0 is allowed (the valid v3 case of
// crossing into a range with no liquidity). Callers must break the swap walk
// after a 0 result — the subsequent singleRangeSwap math divides by L.
// Addresses audit Issue 18 (signed overflow in tick-crossing).
std::optional<std::uint64_t>
applyLiquidityNet(std::uint64_t activeLiquidity, std::uint64_t liquidityNetRaw, bool zeroForOne)
{
    constexpr auto kInt64Max = std::numeric_limits<std::int64_t>::max();
    if (activeLiquidity > static_cast<std::uint64_t>(kInt64Max))
        return std::nullopt;
    auto const al = static_cast<std::int64_t>(activeLiquidity);
    auto const netRaw = static_cast<std::int64_t>(liquidityNetRaw);
    auto const delta = zeroForOne ? -netRaw : netRaw;
    std::int64_t newLiq;
#if defined(__has_builtin) && __has_builtin(__builtin_add_overflow)
    if (__builtin_add_overflow(al, delta, &newLiq))
        return std::nullopt;
#else
    if ((delta > 0 && al > kInt64Max - delta) ||
        (delta < 0 && al < std::numeric_limits<std::int64_t>::min() - delta))
        return std::nullopt;
    newLiq = al + delta;
#endif
    if (newLiq < 0)
        return std::nullopt;
    return static_cast<std::uint64_t>(newLiq);
}

struct NextTickInfo
{
    std::int32_t index;
    std::shared_ptr<SLE const> sle;
};

// ─── Tick bitmap helpers ───────────────────────────────────────────────
//
// 256 ticks per word, packed as a uint256. Bit i within a word maps to
// tick (wordIndex * 256 + i - 887272). Offset binary keeps all arithmetic
// in unsigned domain — avoids signed-division surprises around 0.
//
// Bit convention: bit 0 = LSB of byte 0 in the uint256's storage. The
// convention is internal — we read/write the bits using the same scheme,
// so endianness of the underlying base_uint storage doesn't matter for
// correctness as long as it's consistent.

constexpr std::uint16_t kBitmapMaxWordIndex =
    static_cast<std::uint16_t>((kTickBitmapOffset * 2) >> 8);  // 6931

inline bool
bitmapIsSet(uint256 const& bits, std::uint8_t pos) noexcept
{
    return ((bits.data()[pos / 8]) >> (pos % 8)) & 1u;
}

inline void
bitmapSet(uint256& bits, std::uint8_t pos) noexcept
{
    bits.data()[pos / 8] |= static_cast<std::uint8_t>(1u << (pos % 8));
}

inline void
bitmapClear(uint256& bits, std::uint8_t pos) noexcept
{
    bits.data()[pos / 8] &= static_cast<std::uint8_t>(~(1u << (pos % 8)));
}

inline bool
bitmapAllZero(uint256 const& bits) noexcept
{
    for (std::size_t b = 0; b < uint256::kBytes; ++b)
        if (bits.data()[b])
            return false;
    return true;
}

// Find the highest set bit at position <= startPos in `bits`. Returns
// std::nullopt if no bit is set at-or-below startPos.
inline std::optional<std::uint8_t>
bitmapHighestSetBitAtOrBelow(uint256 bits, std::uint8_t startPos) noexcept
{
    // Mask off bits ABOVE startPos.
    int const startByte = startPos / 8;
    int const startBit = startPos % 8;
    if (startBit < 7)
        bits.data()[startByte] &=
            static_cast<std::uint8_t>((1u << (startBit + 1)) - 1);
    for (int b = startByte + 1; b < static_cast<int>(uint256::kBytes); ++b)
        bits.data()[b] = 0;
    // Now find highest set bit overall.
    for (int b = startByte; b >= 0; --b)
    {
        std::uint8_t const byte = bits.data()[b];
        if (byte)
        {
            int const inByte =
                31 - __builtin_clz(static_cast<unsigned int>(byte));
            return static_cast<std::uint8_t>(b * 8 + inByte);
        }
    }
    return std::nullopt;
}

// Find the lowest set bit at position >= startPos in `bits`.
inline std::optional<std::uint8_t>
bitmapLowestSetBitAtOrAbove(uint256 bits, std::uint8_t startPos) noexcept
{
    int const startByte = startPos / 8;
    int const startBit = startPos % 8;
    for (int b = 0; b < startByte; ++b)
        bits.data()[b] = 0;
    if (startBit > 0)
        bits.data()[startByte] &=
            static_cast<std::uint8_t>(~((1u << startBit) - 1));
    for (int b = startByte; b < static_cast<int>(uint256::kBytes); ++b)
    {
        std::uint8_t const byte = bits.data()[b];
        if (byte)
        {
            int const inByte = __builtin_ctz(static_cast<unsigned int>(byte));
            return static_cast<std::uint8_t>(b * 8 + inByte);
        }
    }
    return std::nullopt;
}

// Walk to the adjacent initialised tick in the swap direction. Returns
// the tick index plus its SLE so callers don't re-descend the SHAMap to
// read sfLiquidityNet (or, in the apply path, prime the cache for the
// peek-for-mutation).
//
// Two implementations live here:
//   - findNextTickByDir: the original SHAMap pred/succ walk over individual
//     tick keylets. O(log N) per crossing.
//   - findNextTickByBitmap: bit-scan over the per-256-tick presence bitmap.
//     O(1) inside one word; one SHAMap descent per word boundary crossed.
//     For clustered liquidity (typical CL), this is ~10-100× cheaper.
//
// The public findNextTick dispatches based on whether the pool has a
// populated bitmap. CL pools that exist before this code lands have no
// bitmap; they fall back to the directory walk. New pools (post code-land
// of bitmap maintenance in AMMDeposit / AMMWithdraw) have a bitmap.

namespace {

std::optional<NextTickInfo>
findNextTickByDir(
    ReadView const& view,
    uint256 const& ammID,
    std::int32_t currentTick,
    bool zeroForOne)
{
    auto const tickKey = keylet::ammTick(ammID, currentTick);
    auto const base = keylet::ammTickBase(ammID);
    auto const end = keylet::ammTickEnd(ammID);

    auto const adjacent = zeroForOne ? view.pred(tickKey.key, base.key)
                                      : view.succ(tickKey.key, end.key);
    if (!adjacent || *adjacent <= base.key || *adjacent >= end.key)
        return std::nullopt;

    auto tickSle = view.read(keylet::ammTick(*adjacent));
    if (!tickSle || !tickSle->isFieldPresent(sfTickIndex))
        return std::nullopt;
    auto const idx = tickSle->getFieldI32(sfTickIndex);
    return NextTickInfo{idx, std::move(tickSle)};
}

std::optional<NextTickInfo>
findNextTickByBitmap(
    ReadView const& view,
    uint256 const& ammID,
    std::int32_t currentTick,
    bool zeroForOne)
{
    if (zeroForOne)
    {
        // Strictly less than currentTick.
        std::int32_t scanTick = currentTick - 1;
        while (scanTick >= minTick)
        {
            auto const [wordIdx, startBit] = tickToBitmapPos(scanTick);
            auto const bmSle =
                view.read(keylet::ammTickBitmapWord(ammID, wordIdx));
            if (bmSle)
            {
                uint256 const bits{bmSle->getFieldH256(sfBitmapBits)};
                auto const found = bitmapHighestSetBitAtOrBelow(bits, startBit);
                if (found)
                {
                    auto const tick = bitmapPosToTick(wordIdx, *found);
                    auto tickSle = view.read(keylet::ammTick(ammID, tick));
                    if (tickSle)
                        return NextTickInfo{tick, std::move(tickSle)};
                    // Bitmap says set but tick SLE missing — invariant
                    // violation. Skip and continue rather than corrupt
                    // the walk.
                    scanTick = tick - 1;
                    continue;
                }
            }
            if (wordIdx == 0)
                break;
            scanTick = bitmapPosToTick(
                static_cast<std::uint16_t>(wordIdx - 1), 255);
        }
        return std::nullopt;
    }
    // Strictly greater than currentTick.
    std::int32_t scanTick = currentTick + 1;
    while (scanTick <= maxTick)
    {
        auto const [wordIdx, startBit] = tickToBitmapPos(scanTick);
        auto const bmSle =
            view.read(keylet::ammTickBitmapWord(ammID, wordIdx));
        if (bmSle)
        {
            uint256 const bits{bmSle->getFieldH256(sfBitmapBits)};
            auto const found = bitmapLowestSetBitAtOrAbove(bits, startBit);
            if (found)
            {
                auto const tick = bitmapPosToTick(wordIdx, *found);
                auto tickSle = view.read(keylet::ammTick(ammID, tick));
                if (tickSle)
                    return NextTickInfo{tick, std::move(tickSle)};
                scanTick = tick + 1;
                continue;
            }
        }
        if (wordIdx >= kBitmapMaxWordIndex)
            break;
        scanTick =
            bitmapPosToTick(static_cast<std::uint16_t>(wordIdx + 1), 0);
    }
    return std::nullopt;
}

// Does this pool have a populated tick bitmap? Cheap probe — one keylet
// read at the bitmap base range. We use succ from the (empty) base key
// to find the first bitmap word; if any exists, the pool is on the
// bitmap path. Result can be cached by the caller per swap if needed.
bool
poolUsesTickBitmap(ReadView const& view, uint256 const& ammID)
{
    auto const base = keylet::ammTickBitmapBase(ammID);
    auto const end = keylet::ammTickBitmapEnd(ammID);
    auto const first = view.succ(base.key, end.key);
    return first.has_value() && *first > base.key && *first < end.key;
}

}  // namespace

std::optional<NextTickInfo>
findNextTick(ReadView const& view, uint256 const& ammID, std::int32_t currentTick, bool zeroForOne)
{
    if (poolUsesTickBitmap(view, ammID))
        return findNextTickByBitmap(view, ammID, currentTick, zeroForOne);
    return findNextTickByDir(view, ammID, currentTick, zeroForOne);
}

//-----------------------------------------------------------------------
// CurveType 0: ConstantProduct
//-----------------------------------------------------------------------
class ConstantProductCurve final : public CurveInterface
{
public:
    std::expected<STAmount, TER>
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
            return std::unexpected(tecAMM_FAILED);

        Number const out = poolOut - num / denom;
        if (out <= Number{0})
            return std::unexpected(tecAMM_FAILED);

        NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
        return toSTAmount(poolOut.asset(), out);
    }

    std::expected<STAmount, TER>
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
            return std::unexpected(tecAMM_FAILED);

        Number const in = (poolIn * poolOut / denom - poolIn) / f;
        if (in <= Number{0})
            return std::unexpected(tecAMM_FAILED);

        NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
        return toSTAmount(poolIn.asset(), in);
    }

    std::expected<Number, TER>
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

    std::expected<STAmount, TER>
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
    std::expected<STAmount, TER>
    swapIn(
        STAmount const& poolIn,
        STAmount const& poolOut,
        STAmount const& assetIn,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& cctx = {}) const override
    {
        if (ammSle == nullptr)
            return std::unexpected(tecINTERNAL);

        auto activeLiquidity = ammSle->getFieldU64(sfActiveLiquidity);
        if (activeLiquidity == 0)
            return std::unexpected(tecAMM_FAILED);

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
                return std::unexpected(tecAMM_FAILED);
            NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
            return toSTAmount(poolOut.asset(), out);
        }

        Number remainingIn = feeAdjustedIn;
        Number totalOut{0};
        int maxCrosses = maxTickCrossings;

        while (remainingIn > Number{0})
        {
            if (maxCrosses == 0)
            {
                if (cctx.tickCapHit)
                    *cctx.tickCapHit = true;
                break;
            }
            --maxCrosses;

            auto const next = findNextTick(*cctx.view, *cctx.ammID, currentTick, zeroForOne);
            if (!next)
                break;

            Number const sqrtPTarget = tickToSqrtPrice(next->index);
            Number const toTarget = inputToTickBoundary(l, sqrtP, sqrtPTarget, zeroForOne);

            // Strict-less: when remainingIn exactly fills the range, fall
            // through to the crossing branch so state advances past the
            // boundary. Required for audit #19's cap-aware offer sizing —
            // BookStep iterates per-range offers, each of which exactly
            // saturates the current range; without crossing on equality,
            // the AMM state would stick at the boundary and the next
            // offer query would return the same offer forever.
            if (toTarget < Number{0} || remainingIn < toTarget)
            {
                auto const [_, out] = singleRangeSwapIn(l, sqrtP, remainingIn, zeroForOne);
                totalOut = totalOut + out;
                remainingIn = Number{0};
                break;
            }

            totalOut = totalOut + outputAtTickBoundary(l, sqrtP, sqrtPTarget, zeroForOne);
            remainingIn = remainingIn - toTarget;
            sqrtP = sqrtPTarget;

            // Reuse the SLE findNextTick already fetched — no second
            // SHAMap descent for the liquidityNet read.
            auto const updated =
                applyLiquidityNet(activeLiquidity, next->sle->getFieldU64(sfLiquidityNet), zeroForOne);
            if (!updated)
                break;
            activeLiquidity = *updated;
            currentTick = next->index;
            if (activeLiquidity == 0)
                break;
            l = Number(activeLiquidity);
        }

        if (totalOut <= Number{0})
            return std::unexpected(tecAMM_FAILED);

        NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
        return toSTAmount(poolOut.asset(), totalOut);
    }

    std::expected<STAmount, TER>
    swapOut(
        STAmount const& poolIn,
        STAmount const& poolOut,
        STAmount const& assetOut,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& cctx = {}) const override
    {
        if (ammSle == nullptr)
            return std::unexpected(tecINTERNAL);

        auto activeLiquidity = ammSle->getFieldU64(sfActiveLiquidity);
        if (activeLiquidity == 0)
            return std::unexpected(tecAMM_FAILED);

        auto const f = feeMult(tfee);
        bool const zeroForOne = poolIn.asset() < poolOut.asset();

        auto currentTick = ammSle->getFieldI32(sfCurrentTick);

        Number l(activeLiquidity);
        Number sqrtP = tickToSqrtPrice(currentTick);

        if ((cctx.view == nullptr) || (cctx.ammID == nullptr))
        {
            auto const [_, in_] = singleRangeSwapOut(l, sqrtP, Number(assetOut), zeroForOne);
            if (in_ <= Number{0})
                return std::unexpected(tecAMM_FAILED);
            NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
            return toSTAmount(poolIn.asset(), in_ / f);
        }

        Number remainingOut = Number(assetOut);
        Number totalIn{0};
        int maxCrosses = maxTickCrossings;

        while (remainingOut > Number{0})
        {
            if (maxCrosses == 0)
            {
                if (cctx.tickCapHit)
                    *cctx.tickCapHit = true;
                break;
            }
            --maxCrosses;

            auto const next = findNextTick(*cctx.view, *cctx.ammID, currentTick, zeroForOne);
            if (!next)
                break;

            Number const sqrtPTarget = tickToSqrtPrice(next->index);
            Number const maxOut = outputAtTickBoundary(l, sqrtP, sqrtPTarget, zeroForOne);

            // Strict-less for the same reason as swapIn: exact boundary
            // fills must cross so subsequent offer queries see the new
            // range. See comment in swapIn above (audit #19).
            if (maxOut < Number{0} || remainingOut < maxOut)
            {
                auto const [_, in_] = singleRangeSwapOut(l, sqrtP, remainingOut, zeroForOne);
                if (in_ <= Number{0})
                    return std::unexpected(tecAMM_FAILED);
                totalIn = totalIn + in_;
                remainingOut = Number{0};
                break;
            }

            totalIn = totalIn + inputToTickBoundary(l, sqrtP, sqrtPTarget, zeroForOne);
            remainingOut = remainingOut - maxOut;
            sqrtP = sqrtPTarget;

            auto const updated =
                applyLiquidityNet(activeLiquidity, next->sle->getFieldU64(sfLiquidityNet), zeroForOne);
            if (!updated)
                break;
            activeLiquidity = *updated;
            currentTick = next->index;
            if (activeLiquidity == 0)
                break;
            l = Number(activeLiquidity);
        }

        if (totalIn <= Number{0})
            return std::unexpected(tecAMM_FAILED);

        totalIn = totalIn / f;

        NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
        return toSTAmount(poolIn.asset(), totalIn);
    }

    std::expected<Number, TER>
    spotPrice(
        STAmount const& poolIn,
        STAmount const& poolOut,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& = {}) const override
    {
        if (ammSle == nullptr)
            return std::unexpected(tecINTERNAL);

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

    std::expected<STAmount, TER>
    initialLPTokens(
        STAmount const& asset1,
        STAmount const& asset2,
        Issue const& lptIssue,
        STObject const*) const override
    {
        return ammLPTokens(asset1, asset2, lptIssue);
    }

    // CL pool product (Uniswap v3 virtual reserves) requires knowing
    // sqrtP_lower and sqrtP_upper of the active range, which depends on
    // which initialized ticks bracket the current price. Computing that
    // accurately means running findNextTick on both sides — doable but
    // adds two SLE reads per consumeOffer. The cheap check here is
    // direction: the pool must have gained input asset and lost output
    // asset (or both unchanged for a no-op). The structural invariants
    // on the AMM SLE itself (sfPositionCount/sfActiveLiquidity coherence,
    // feeGrowthGlobal monotonicity) are enforced separately by ValidAMM.
    // Full per-tick K conservation (audit #23) is blocked on a per-AMM
    // positions index — TODO.
    bool
    checkInvariant(
        STAmount const& oldIn,
        STAmount const& oldOut,
        STAmount const& newIn,
        STAmount const& newOut,
        STObject const*) const override
    {
        // Allow no-op (both sides unchanged).
        if (newIn == oldIn && newOut == oldOut)
            return true;
        // Pool gained input, lost output. Strict-greater on the input
        // and strict-less on the output side; equality on one with
        // change on the other is malformed.
        return newIn >= oldIn && newOut <= oldOut &&
            !(newIn == oldIn && newOut < oldOut) &&
            !(newOut == oldOut && newIn > oldIn);
    }

    // Mirror the tick traversal performed by swapIn() against the AMM SLE so
    // that pool state advances with each realized swap. Without this, the
    // curve quotes against the create-time state forever and fees never
    // accrue. Trustline balances are updated by BookStep::consumeOffer
    // independently; this function only writes the AMM-derived state
    // (currentTick / activeLiquidity / feeGrowthGlobal0|1) and tick SLE
    // feeGrowthOutside flips.
    //
    // Within-tick price precision is not tracked: the next swap re-reads
    // sqrtP via tickToSqrtPrice(sfCurrentTick), so a swap that stays inside
    // a range advances feeGrowth but leaves the price snapped to the
    // starting tick boundary. This matches the existing quote-path
    // behaviour. Tracking the precise within-range sqrtPrice is a separate
    // follow-up; sfSqrtPriceX96 today is unused (always zero).
    TER
    applySwap(
        ApplyView& view,
        uint256 const& ammID,
        STAmount const& assetIn,
        STAmount const& assetOut,
        std::uint16_t tfee,
        STObject const*) const override
    {
        auto ammSle = view.peek(keylet::amm(ammID));
        if (!ammSle)
            return tecINTERNAL;

        auto activeLiquidity = ammSle->getFieldU64(sfActiveLiquidity);
        if (activeLiquidity == 0)
            return tecINTERNAL;

        auto currentTick = ammSle->getFieldI32(sfCurrentTick);

        Number feeGrowthGlobal0{ammSle->getFieldNumber(sfFeeGrowthGlobal0)};
        Number feeGrowthGlobal1{ammSle->getFieldNumber(sfFeeGrowthGlobal1)};

        bool const zeroForOne = assetIn.asset() < assetOut.asset();

        auto const f = feeMult(tfee);
        Number const grossIn{assetIn};
        Number const feeAdjustedTotal = grossIn * f;
        Number const totalFee = grossIn - feeAdjustedTotal;
        if (feeAdjustedTotal <= Number{0})
            return tecINTERNAL;

        Number l(activeLiquidity);
        Number sqrtP = tickToSqrtPrice(currentTick);

        Number& feeGrowthIn = zeroForOne ? feeGrowthGlobal0 : feeGrowthGlobal1;

        Number remainingIn = feeAdjustedTotal;
        int maxCrosses = maxTickCrossings;
        bool capHit = false;
        (void)assetOut;  // output drives trustline transfers in BookStep; not needed here.

        while (remainingIn > Number{0})
        {
            if (maxCrosses == 0)
            {
                capHit = true;
                break;
            }
            --maxCrosses;

            auto const next = findNextTick(view, ammID, currentTick, zeroForOne);
            if (!next)
                break;

            Number const sqrtPTarget = tickToSqrtPrice(next->index);
            Number const toTarget = inputToTickBoundary(l, sqrtP, sqrtPTarget, zeroForOne);

            // Strict-less: when remainingIn exactly fills the range, fall
            // through to the crossing branch so state advances past the
            // boundary. Required for audit #19's cap-aware offer sizing —
            // BookStep iterates per-range offers, each of which exactly
            // saturates the current range; without crossing on equality,
            // the AMM state would stick at the boundary and the next
            // offer query would return the same offer forever.
            if (toTarget < Number{0} || remainingIn < toTarget)
            {
                // Final segment: swap stops inside the current range.
                Number const segmentFee = (remainingIn / feeAdjustedTotal) * totalFee;
                feeGrowthIn = feeGrowthIn + segmentFee / l;
                remainingIn = Number{0};
                break;
            }

            // Crossing: allocate fee share for the segment, then commit
            // the cross — flip the boundary's outside snapshots, apply
            // liquidityNet, advance currentTick. The liquidity update is
            // checked first against the read-only SLE returned by
            // findNextTick (no second SHAMap descent) so an overflow
            // stops the walk before any partial cross is observable.
            Number const segmentFee = (toTarget / feeAdjustedTotal) * totalFee;
            feeGrowthIn = feeGrowthIn + segmentFee / l;
            remainingIn = remainingIn - toTarget;
            sqrtP = sqrtPTarget;

            auto const updated =
                applyLiquidityNet(activeLiquidity, next->sle->getFieldU64(sfLiquidityNet), zeroForOne);
            if (!updated)
                break;

            // Only peek for mutation now that the check has succeeded.
            // The read above primed the view's cache for this key, so the
            // peek is a cache hit.
            auto tickSle = view.peek(keylet::ammTick(ammID, next->index));
            if (!tickSle)
                break;

            Number const outside0{tickSle->getFieldNumber(sfFeeGrowthOutside0)};
            Number const outside1{tickSle->getFieldNumber(sfFeeGrowthOutside1)};
            tickSle->setFieldNumber(
                sfFeeGrowthOutside0, STNumber{sfFeeGrowthOutside0, feeGrowthGlobal0 - outside0});
            tickSle->setFieldNumber(
                sfFeeGrowthOutside1, STNumber{sfFeeGrowthOutside1, feeGrowthGlobal1 - outside1});
            view.update(tickSle);

            activeLiquidity = *updated;
            currentTick = next->index;
            if (activeLiquidity == 0)
                break;
            l = Number(activeLiquidity);
        }

        ammSle->setFieldI32(sfCurrentTick, currentTick);
        ammSle->setFieldU64(sfActiveLiquidity, activeLiquidity);
        ammSle->setFieldNumber(
            sfFeeGrowthGlobal0, STNumber{sfFeeGrowthGlobal0, feeGrowthGlobal0});
        ammSle->setFieldNumber(
            sfFeeGrowthGlobal1, STNumber{sfFeeGrowthGlobal1, feeGrowthGlobal1});
        view.update(ammSle);

        // Cap hit: writeback succeeded with the partial advance; surface
        // the typed code so callers can distinguish this from "ran out of
        // ticks" (audit #20). Today's call site (AMMOffer::consume) turns
        // any non-tesSUCCESS into a FlowException, which propagates up
        // to Payment as a transaction failure.
        if (capHit)
            return tecAMM_TICK_CAP_HIT;
        return tesSUCCESS;
    }
};

//-----------------------------------------------------------------------
// CurveType 2: StableSwap
//-----------------------------------------------------------------------
class StableSwapCurve final : public CurveInterface
{
public:
    std::expected<STAmount, TER>
    swapIn(
        STAmount const& poolIn,
        STAmount const& poolOut,
        STAmount const& assetIn,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& = {}) const override
    {
        if (ammSle == nullptr)
            return std::unexpected(tecINTERNAL);

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
            return std::unexpected(tecAMM_FAILED);

        NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
        return toSTAmount(poolOut.asset(), dy);
    }

    std::expected<STAmount, TER>
    swapOut(
        STAmount const& poolIn,
        STAmount const& poolOut,
        STAmount const& assetOut,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& = {}) const override
    {
        if (ammSle == nullptr)
            return std::unexpected(tecINTERNAL);

        auto const a = Number(ammSle->getFieldU32(sfAmplification));
        auto const f = feeMult(tfee);

        Number const x = poolIn;
        Number const y = poolOut;
        Number const newY = y - Number(assetOut);

        if (newY <= Number{0})
            return std::unexpected(tecAMM_FAILED);

        Number const d = computeD(x, y, a);
        Number const newX = computeY(newY, d, a);
        Number const dx = (newX - x) / f;

        if (dx <= Number{0})
            return std::unexpected(tecAMM_FAILED);

        NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
        return toSTAmount(poolIn.asset(), dx);
    }

    std::expected<Number, TER>
    spotPrice(
        STAmount const& poolIn,
        STAmount const& poolOut,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& = {}) const override
    {
        if (ammSle == nullptr)
            return std::unexpected(tecINTERNAL);

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

    std::expected<STAmount, TER>
    initialLPTokens(
        STAmount const& asset1,
        STAmount const& asset2,
        Issue const& lptIssue,
        STObject const* curveParams) const override
    {
        if (curveParams == nullptr)
            return std::unexpected(tecINTERNAL);

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

// CurveType 3: Binned (Trader Joe LB / Meteora DLMM style)
//-----------------------------------------------------------------------
// Constant-sum within a bin at price p(id) = (1 + binStep/10000)^id.
// Multi-bin walk: when the active bin is depleted in the swap direction,
// advance to the adjacent bin and continue. The walk terminates when
// the input is fully consumed, the next bin in the swap direction has
// no liquidity, or kMaxIterations is hit.
class BinnedCurve final : public CurveInterface
{
    // Per-swap cap on bin crossings. Mirrors CL's maxTickCrossings=1000
    // (AMMCore.h) — same precedent: bound the SHAMap-descent cost per
    // swap so a pathological pool can't stretch the close-time budget,
    // while leaving comfortable headroom for legitimate trades. At
    // binStep=1 (1bp/bin), 1000 crossings spans a ~10% price move; at
    // binStep=10, ~2.7x. Hitting the cap produces a silent partial fill,
    // same convention as CL — see §9.4 of the spec for partial-fill
    // semantics on the caller side.
    static constexpr int kMaxBinIters = 1000;

    // One step of a multi-bin walk: which bin contributed, how much
    // input it consumed, and how much output it produced.
    struct WalkStep
    {
        std::int32_t binID;
        Number dxConsumed;
        Number dyProduced;
    };

    struct WalkResult
    {
        Number totalDx{0};
        Number totalDy{0};
        std::vector<WalkStep> steps;
        std::int32_t finalActiveBinID = 0;
    };

    // Locate the next populated bin in the walk direction. Mutates
    // binID in place to land on the returned bin's index; returns
    // nullptr (and leaves binID at the position where the walk
    // terminated) when no further populated bin exists. "Populated"
    // means non-zero reserve on the OUTPUT side of the swap — a bin
    // with zero outbound reserve cannot contribute and is skipped.
    //
    // The walk uses view.succ()/pred() on the AMM-scoped bin keylet
    // range so sparse pools are O(log n) per crossing, not O(gap).
    //
    // Extracted from walkBins / walkBinsForOut so the SHAMap
    // traversal + offset-binary keylet decode lives in one place.
    static std::shared_ptr<SLE const>
    seekPopulatedBin(
        ReadView const& view,
        uint256 const& ammID,
        std::int32_t& binID,
        std::int32_t direction,
        bool inIsAsset0,
        uint256 const& binsBase,
        uint256 const& binsEnd)
    {
        while (binID >= minBinID && binID <= maxBinID)
        {
            auto binSle = view.read(keylet::ammBin(ammID, binID));
            if (binSle)
            {
                Number const outReserve = inIsAsset0
                    ? Number{binSle->getFieldAmount(sfReserve1)}
                    : Number{binSle->getFieldAmount(sfReserve0)};
                if (outReserve > Number{0})
                    return binSle;
            }
            auto const curKey = keylet::ammBin(ammID, binID).key;
            std::optional<uint256> const nextKey = direction > 0
                ? view.succ(curKey, binsEnd)
                : view.pred(curKey, binsBase);
            if (!nextKey)
                return nullptr;
            // Decode binID from the keylet's offset-encoded low 64 bits.
            std::uint64_t const encoded = getLow64BE(*nextKey);
            binID = static_cast<std::int32_t>(
                static_cast<std::int64_t>(encoded) +
                static_cast<std::int64_t>(minBinID));
        }
        return nullptr;
    }

    // Walk bins from activeBinID in the swap direction, accumulating
    // output. Read-only: no SLE mutation. Both swapIn (quoting) and
    // applySwap (settlement) call this; identical inputs produce
    // identical walks, so settled state matches the quote.
    //
    // `dxBudget` is the post-fee input the swap can spend.
    // `inIsAsset0` is true when the input asset is the canonical
    // lex-min asset; in that case bin output is asset1 = dx * P, and
    // depleting asset1 advances activeBinID upward (next bin has
    // higher P and more asset1). Symmetric for inIsAsset1.
    static WalkResult
    walkBins(
        ReadView const& view,
        uint256 const& ammID,
        std::uint16_t binStep,
        std::int32_t activeBinID,
        bool inIsAsset0,
        Number dxBudget)
    {
        WalkResult res;
        res.finalActiveBinID = activeBinID;
        if (dxBudget <= Number{0})
            return res;

        std::int32_t binID = activeBinID;
        std::int32_t const direction = inIsAsset0 ? 1 : -1;
        Number dxRemaining = dxBudget;
        // SHAMap-succ-based seek: bins for one AMM are contiguous in
        // keylet order (low 64 bits = offset-encoded binID), so
        // `view.succ/pred` jumps to the next populated bin in O(log n)
        // regardless of how sparse the pool is.
        auto const binsEnd = keylet::ammBinEnd(ammID).key;
        auto const binsBase = keylet::ammBinBase(ammID).key;

        for (int iter = 0; iter < kMaxBinIters && dxRemaining > Number{0}; ++iter)
        {
            auto const binSle = seekPopulatedBin(
                view, ammID, binID, direction, inIsAsset0, binsBase, binsEnd);
            if (!binSle)
                break;

            Number const P = binPrice(binStep, binID);
            STAmount const r0 = binSle->getFieldAmount(sfReserve0);
            STAmount const r1 = binSle->getFieldAmount(sfReserve1);

            Number const availableOut = inIsAsset0 ? Number{r1} : Number{r0};
            if (availableOut <= Number{0})
            {
                binID += direction;
                res.finalActiveBinID = binID;
                continue;
            }

            Number const dyAtFull = inIsAsset0 ? dxRemaining * P : dxRemaining / P;
            if (dyAtFull <= availableOut)
            {
                res.steps.push_back({binID, dxRemaining, dyAtFull});
                res.totalDx += dxRemaining;
                res.totalDy += dyAtFull;
                dxRemaining = Number{0};
                res.finalActiveBinID = binID;
                break;
            }

            // This bin caps the output. Consume what it can.
            Number const dxAtCap = inIsAsset0 ? availableOut / P : availableOut * P;
            res.steps.push_back({binID, dxAtCap, availableOut});
            res.totalDx += dxAtCap;
            res.totalDy += availableOut;
            dxRemaining -= dxAtCap;
            binID += direction;
            res.finalActiveBinID = binID;
        }
        return res;
    }

    // Output-driven walk: given a desired dy budget, walk bins finding
    // the dx required to produce that much output. Symmetric to walkBins
    // but iterates against output-side capacity.
    static WalkResult
    walkBinsForOut(
        ReadView const& view,
        uint256 const& ammID,
        std::uint16_t binStep,
        std::int32_t activeBinID,
        bool inIsAsset0,
        Number dyBudget)
    {
        WalkResult res;
        res.finalActiveBinID = activeBinID;
        if (dyBudget <= Number{0})
            return res;

        std::int32_t binID = activeBinID;
        std::int32_t const direction = inIsAsset0 ? 1 : -1;
        Number dyRemaining = dyBudget;
        auto const binsEnd = keylet::ammBinEnd(ammID).key;
        auto const binsBase = keylet::ammBinBase(ammID).key;

        for (int iter = 0; iter < kMaxBinIters && dyRemaining > Number{0}; ++iter)
        {
            auto const binSle = seekPopulatedBin(
                view, ammID, binID, direction, inIsAsset0, binsBase, binsEnd);
            if (!binSle)
                break;

            Number const P = binPrice(binStep, binID);
            STAmount const r0 = binSle->getFieldAmount(sfReserve0);
            STAmount const r1 = binSle->getFieldAmount(sfReserve1);

            Number const availableOut = inIsAsset0 ? Number{r1} : Number{r0};

            if (dyRemaining <= availableOut)
            {
                // Bin can deliver the remaining output.
                Number const dxNeeded = inIsAsset0 ? dyRemaining / P : dyRemaining * P;
                res.steps.push_back({binID, dxNeeded, dyRemaining});
                res.totalDx += dxNeeded;
                res.totalDy += dyRemaining;
                dyRemaining = Number{0};
                res.finalActiveBinID = binID;
                break;
            }

            // Bin caps the output — take what it has and continue.
            Number const dxFull = inIsAsset0 ? availableOut / P : availableOut * P;
            res.steps.push_back({binID, dxFull, availableOut});
            res.totalDx += dxFull;
            res.totalDy += availableOut;
            dyRemaining -= availableOut;
            binID += direction;
            res.finalActiveBinID = binID;
        }
        return res;
    }

    // Compute bin price using fast exponentiation.
    static Number
    binPrice(std::uint16_t binStep, std::int32_t binID)
    {
        Number const stepFactor = Number{1} + Number{binStep} / Number{10000};
        if (binID == 0)
            return Number{1};
        bool const positive = binID > 0;
        std::int64_t n = positive ? binID : -static_cast<std::int64_t>(binID);
        Number result{1};
        Number base = stepFactor;
        while (n > 0)
        {
            if (n & 1)
                result = result * base;
            base = base * base;
            n >>= 1;
        }
        return positive ? result : Number{1} / result;
    }

public:
    std::expected<STAmount, TER>
    swapIn(
        STAmount const& poolIn,
        STAmount const& poolOut,
        STAmount const& assetIn,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& ctx = {}) const override
    {
        if (ammSle == nullptr)
            return std::unexpected(tecINTERNAL);

        auto const binStep = ammSle->getFieldU16(sfBinStep);
        auto const activeBinID = ammSle->getFieldI32(sfActiveBinID);
        auto const f = feeMult(tfee);
        Number const dxAfterFee = Number{assetIn} * f;
        bool const inIsAsset0 = (poolIn.asset() < poolOut.asset());

        Number dy{0};
        if (ctx.view && ctx.ammID)
        {
            auto const walk = walkBins(
                *ctx.view, *ctx.ammID, binStep, activeBinID, inIsAsset0, dxAfterFee);
            dy = walk.totalDy;
        }
        else
        {
            // No view context (e.g. invariant probing). Fall back to
            // single-bin math against poolOut as the cap.
            Number const P = binPrice(binStep, activeBinID);
            dy = inIsAsset0 ? dxAfterFee * P : dxAfterFee / P;
            Number const maxOut = Number{poolOut};
            if (dy > maxOut)
                dy = maxOut;
        }

        if (dy <= Number{0})
            return std::unexpected(tecAMM_FAILED);

        NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
        return toSTAmount(poolOut.asset(), dy);
    }

    std::expected<STAmount, TER>
    swapOut(
        STAmount const& poolIn,
        STAmount const& poolOut,
        STAmount const& assetOut,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& ctx = {}) const override
    {
        if (ammSle == nullptr)
            return std::unexpected(tecINTERNAL);

        if (Number{assetOut} > Number{poolOut})
            return std::unexpected(tecAMM_FAILED);

        auto const binStep = ammSle->getFieldU16(sfBinStep);
        auto const activeBinID = ammSle->getFieldI32(sfActiveBinID);
        auto const f = feeMult(tfee);
        bool const inIsAsset0 = (poolIn.asset() < poolOut.asset());

        Number dxPreFee{0};
        if (ctx.view && ctx.ammID)
        {
            auto const walk = walkBinsForOut(
                *ctx.view, *ctx.ammID, binStep, activeBinID, inIsAsset0, Number{assetOut});
            // Walk must deliver the FULL output to honor the swapOut
            // contract — if any output remains undelivered, the AMM
            // can't fulfil the swap.
            if (walk.totalDy < Number{assetOut})
                return std::unexpected(tecAMM_FAILED);
            dxPreFee = walk.totalDx;
        }
        else
        {
            // No view context — single-bin fallback.
            Number const P = binPrice(binStep, activeBinID);
            dxPreFee = inIsAsset0 ? Number{assetOut} / P : Number{assetOut} * P;
        }

        Number const dx = dxPreFee / f;
        if (dx <= Number{0})
            return std::unexpected(tecAMM_FAILED);

        NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
        return toSTAmount(poolIn.asset(), dx);
    }

    std::expected<Number, TER>
    spotPrice(
        STAmount const& poolIn,
        STAmount const& poolOut,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& = {}) const override
    {
        if (ammSle == nullptr)
            return std::unexpected(tecINTERNAL);
        auto const binStep = ammSle->getFieldU16(sfBinStep);
        auto const activeBinID = ammSle->getFieldI32(sfActiveBinID);
        Number const P = binPrice(binStep, activeBinID);

        bool const inIsAsset0 = (poolIn.asset() < poolOut.asset());
        Number const direction = inIsAsset0 ? P : Number{1} / P;
        auto const f = feeMult(tfee);
        return direction / f;
    }

    [[nodiscard]] TER
    validateParams(STObject const& curveParams) const override
    {
        if (!curveParams.isFieldPresent(sfBinStep))
            return temMALFORMED;
        auto const step = curveParams.getFieldU16(sfBinStep);
        for (std::uint8_t i = 0; i < binStepCount; ++i)
            if (validBinSteps[i] == step)
                return tesSUCCESS;
        return temMALFORMED;
    }

    std::expected<STAmount, TER>
    initialLPTokens(
        STAmount const&,
        STAmount const&,
        Issue const& lptIssue,
        STObject const*) const override
    {
        // Binned pools mint no aggregate LP tokens; shares are per-bin.
        return STAmount{lptIssue, 0};
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
        auto const binStep = ammSle->getFieldU16(sfBinStep);
        auto const activeBinID = ammSle->getFieldI32(sfActiveBinID);
        Number const P = binPrice(binStep, activeBinID);

        // Constant-sum at bin price: in*P + out (if in=asset0) must be
        // non-decreasing (trading fee can only add). Compute against the
        // direction of asset0 in poolIn.
        bool const inIsAsset0 = (oldIn.asset() < oldOut.asset());
        Number const oldSum = inIsAsset0
            ? Number{oldIn} * P + Number{oldOut}
            : Number{oldOut} * P + Number{oldIn};
        Number const newSum = inIsAsset0
            ? Number{newIn} * P + Number{newOut}
            : Number{newOut} * P + Number{newIn};
        return newSum >= oldSum ||
            withinRelativeDistance(oldSum, newSum, Number{1, -7});
    }

    // applySwap: walk bins against the REALIZED output BookStep
    // delivered to the user, mutating each touched bin's reserves to
    // honor exactly that output and the corresponding input. The output
    // is the ground truth from BookStep — pricing it back through bins
    // tells us where the input goes. Advance sfActiveBinID afterward.
    TER
    applySwap(
        ApplyView& view,
        uint256 const& ammID,
        STAmount const& assetIn,
        STAmount const& assetOut,
        std::uint16_t /*tfee*/,
        STObject const* ammSle) const override
    {
        if (ammSle == nullptr)
            return tecINTERNAL;
        auto const binStep = ammSle->getFieldU16(sfBinStep);
        auto const activeBinID = ammSle->getFieldI32(sfActiveBinID);
        bool const inIsAsset0 = (assetIn.asset() < assetOut.asset());

        // Walk OUTPUT-driven against the realized assetOut from BookStep.
        // This guarantees sum(bin_dy) == assetOut exactly, matching the
        // trustline movement BookStep already applied. The corresponding
        // dx from each step may not sum to assetIn exactly (due to the
        // single-quality-offer approximation BookStep makes); any leftover
        // input is absorbed pro-rata into the touched bins below.
        auto const walk = walkBinsForOut(
            view, ammID, binStep, activeBinID, inIsAsset0, Number{assetOut});

        // Distribute the realized input/output across the walked bins.
        // If walk yielded zero (e.g. no bin liquidity), fall back to
        // mutating the active bin directly — single-bin behavior.
        if (walk.steps.empty())
        {
            auto binSle = view.peek(keylet::ammBin(ammID, activeBinID));
            if (!binSle)
                return tecINTERNAL;
            auto const r0 = binSle->getFieldAmount(sfReserve0);
            auto const r1 = binSle->getFieldAmount(sfReserve1);
            if (inIsAsset0)
            {
                binSle->setFieldAmount(sfReserve0, r0 + assetIn);
                binSle->setFieldAmount(sfReserve1, r1 - assetOut);
            }
            else
            {
                binSle->setFieldAmount(sfReserve1, r1 + assetIn);
                binSle->setFieldAmount(sfReserve0, r0 - assetOut);
            }
            view.update(binSle);
            return tesSUCCESS;
        }

        // inputScale captures the trading fee. walkBinsForOut computes
        // the no-fee dx required to produce assetOut (i.e. dy/P at each
        // bin). BookStep delivers an assetIn that's higher by 1/(1-fee)
        // because the offer's quality bakes the fee in. The ratio
        //   inputScale = assetIn / walk.totalDx ≈ 1/(1-fee_rate)
        // pins sum(bin_dx) to the realized assetIn so the per-bin
        // invariant stays satisfied AND the per-step (scaledDx − dx)
        // delta is exactly the fee credited to feeGrowthBin below.
        Number const inputScale = (walk.totalDx > Number{0})
            ? Number{assetIn} / walk.totalDx
            : Number{1};
        for (auto const& step : walk.steps)
        {
            auto binSle = view.peek(keylet::ammBin(ammID, step.binID));
            if (!binSle)
                return tecINTERNAL;
            auto const r0 = binSle->getFieldAmount(sfReserve0);
            auto const r1 = binSle->getFieldAmount(sfReserve1);

            Number const scaledDx = step.dxConsumed * inputScale;
            STAmount const dxAmt = toSTAmount(assetIn.asset(), scaledDx);
            STAmount const dyAmt = toSTAmount(assetOut.asset(), step.dyProduced);
            if (inIsAsset0)
            {
                binSle->setFieldAmount(sfReserve0, r0 + dxAmt);
                binSle->setFieldAmount(sfReserve1, r1 - dyAmt);
            }
            else
            {
                binSle->setFieldAmount(sfReserve1, r1 + dxAmt);
                binSle->setFieldAmount(sfReserve0, r0 - dyAmt);
            }

            // Per-step fee = scaledDx - step.dxConsumed (the surplus
            // above the bin's marginal cost). Distribute to the bin's
            // feeGrowth accumulator on the input side.
            Number const stepFee = scaledDx - step.dxConsumed;
            auto const outstanding = binSle->getFieldU64(sfOutstandingAmount);
            if (stepFee > Number{0} && outstanding > 0)
            {
                Number const feePerShare =
                    stepFee / Number{static_cast<std::int64_t>(outstanding)};
                if (inIsAsset0)
                {
                    Number const prevFG =
                        Number{binSle->getFieldNumber(sfFeeGrowthBin0)};
                    binSle->setFieldNumber(
                        sfFeeGrowthBin0,
                        STNumber{sfFeeGrowthBin0, prevFG + feePerShare});
                }
                else
                {
                    Number const prevFG =
                        Number{binSle->getFieldNumber(sfFeeGrowthBin1)};
                    binSle->setFieldNumber(
                        sfFeeGrowthBin1,
                        STNumber{sfFeeGrowthBin1, prevFG + feePerShare});
                }
            }

            view.update(binSle);
        }

        // Advance the AMM's activeBinID to wherever the walk landed.
        if (walk.finalActiveBinID != activeBinID)
        {
            auto ammPeek = view.peek(keylet::amm(ammID));
            if (ammPeek)
            {
                ammPeek->setFieldI32(sfActiveBinID, walk.finalActiveBinID);
                view.update(ammPeek);
            }
        }
        return tesSUCCESS;
    }
};

// Singletons
ConstantProductCurve const kConstantProductCurve;
ConcentratedLiquidityCurve const kConcentratedLiquidityCurve;
StableSwapCurve const kStableSwapCurve;
BinnedCurve const kBinnedCurve;

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

        case CtBinned:
            if (rules.enabled(featureAMMCurves))
                return &kBinnedCurve;
            return nullptr;

        default:
            return nullptr;
    }
}

TER
setTickBitmap(ApplyView& view, uint256 const& ammID, std::int32_t tick, beast::Journal j)
{
    auto const [wordIdx, bitInWord] = tickToBitmapPos(tick);
    auto const wordKeylet = keylet::ammTickBitmapWord(ammID, wordIdx);
    auto sle = view.peek(wordKeylet);
    if (sle)
    {
        uint256 bits{sle->getFieldH256(sfBitmapBits)};
        if (bitmapIsSet(bits, bitInWord))
            return tesSUCCESS;  // already set; idempotent
        bitmapSet(bits, bitInWord);
        sle->setFieldH256(sfBitmapBits, bits);
        view.update(sle);
        return tesSUCCESS;
    }
    // First initialised tick in this 256-tick window — create the SLE.
    sle = std::make_shared<SLE>(wordKeylet);
    (*sle)[sfAMMID] = ammID;
    sle->setFieldU16(sfBitmapWordIndex, wordIdx);
    uint256 bits{};
    bitmapSet(bits, bitInWord);
    sle->setFieldH256(sfBitmapBits, bits);
    sle->setFieldU64(sfOwnerNode, 0);  // bitmap SLEs aren't placed in any directory
    view.insert(sle);
    (void)j;
    return tesSUCCESS;
}

TER
clearTickBitmap(ApplyView& view, uint256 const& ammID, std::int32_t tick, beast::Journal j)
{
    auto const [wordIdx, bitInWord] = tickToBitmapPos(tick);
    auto const wordKeylet = keylet::ammTickBitmapWord(ammID, wordIdx);
    auto sle = view.peek(wordKeylet);
    if (!sle)
        return tesSUCCESS;  // nothing to clear; idempotent
    uint256 bits{sle->getFieldH256(sfBitmapBits)};
    if (!bitmapIsSet(bits, bitInWord))
        return tesSUCCESS;  // already clear
    bitmapClear(bits, bitInWord);
    if (bitmapAllZero(bits))
    {
        // No initialised ticks remain in this 256-tick window — delete
        // the SLE so subsequent succ/pred walks skip the empty space.
        view.erase(sle);
    }
    else
    {
        sle->setFieldH256(sfBitmapBits, bits);
        view.update(sle);
    }
    (void)j;
    return tesSUCCESS;
}

std::optional<Number>
maxBinnedOutputAtActiveBin(
    ReadView const& view,
    uint256 const& ammID,
    STObject const& ammSle,
    bool inIsAsset0)
{
    if (!ammSle.isFieldPresent(sfActiveBinID))
        return std::nullopt;
    auto const activeBinID = ammSle.getFieldI32(sfActiveBinID);
    auto const binSle = view.read(keylet::ammBin(ammID, activeBinID));
    if (!binSle)
        return std::nullopt;
    // Output side: asset1 reserve if input was asset0; else asset0.
    auto const outReserve = inIsAsset0
        ? binSle->getFieldAmount(sfReserve1)
        : binSle->getFieldAmount(sfReserve0);
    return Number{outReserve};
}

std::optional<Number>
maxClOutputWithinCurrentRange(
    ReadView const& view,
    uint256 const& ammID,
    STObject const& ammSle,
    bool zeroForOne)
{
    if (!ammSle.isFieldPresent(sfActiveLiquidity) ||
        !ammSle.isFieldPresent(sfCurrentTick))
        return std::nullopt;
    auto const activeLiq = ammSle.getFieldU64(sfActiveLiquidity);
    if (activeLiq == 0)
        return Number{0};

    auto const currentTick = ammSle.getFieldI32(sfCurrentTick);
    auto const next = findNextTick(view, ammID, currentTick, zeroForOne);
    if (!next)
        return std::nullopt;  // No further tick — no cap; pool reserves bound

    Number const L(activeLiq);
    Number const sqrtP = tickToSqrtPrice(currentTick);
    Number const sqrtPTarget = tickToSqrtPrice(next->index);
    if (zeroForOne)
    {
        // Price decreases; output is asset1: L * (sqrtP - sqrtPTarget)
        if (sqrtP <= sqrtPTarget)
            return Number{0};
        return L * (sqrtP - sqrtPTarget);
    }
    // Price increases; output is asset0: L * (sqrtPTarget - sqrtP) /
    //                                    (sqrtP * sqrtPTarget)
    if (sqrtPTarget <= sqrtP)
        return Number{0};
    return L * (sqrtPTarget - sqrtP) / (sqrtP * sqrtPTarget);
}

}  // namespace xrpl
