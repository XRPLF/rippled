#include <xrpl/tx/transactors/dex/CLAMMHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/SField.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace xrpl {
namespace clamm {

// ---- SLE Field Conversion Helpers ----

uint128
fromSLEField(base_uint<128> const& field)
{
    uint128 result;
    boost::multiprecision::import_bits(
        result, field.data(), field.data() + 16, 8, true);
    return result;
}

base_uint<128>
toSLEField(uint128 const& value)
{
    base_uint<128> result{};
    std::vector<unsigned char> bytes;
    boost::multiprecision::export_bits(
        value, std::back_inserter(bytes), 8, true);

    auto const offset = 16 - std::min<std::size_t>(bytes.size(), 16);
    for (std::size_t i = 0; i < bytes.size() && (offset + i) < 16; ++i)
        result.data()[offset + i] = bytes[i];

    return result;
}

int128
fromSLEFieldSigned(base_uint<128> const& field)
{
    auto const uval = fromSLEField(field);
    static uint128 const signBit = uint128(1) << 127;
    if (uval >= signBit)
    {
        static uint128 const maxVal = uint128(1) << 127;
        return -static_cast<int128>(maxVal - (uval - maxVal));
    }
    return static_cast<int128>(uval);
}

base_uint<128>
toSLEFieldSigned(int128 const& value)
{
    uint128 uval;
    if (value >= 0)
    {
        uval = static_cast<uint128>(value);
    }
    else
    {
        static uint128 const mod = (uint128(1) << 128);
        uval = mod + static_cast<uint128>(value);
    }
    return toSLEField(uval);
}

// ---- Tick Math ----

uint128
tickToSqrtPrice(std::int32_t tick)
{
    XRPL_ASSERT(
        tick >= CLAMM_MIN_TICK && tick <= CLAMM_MAX_TICK,
        "xrpl::clamm::tickToSqrtPrice : tick in range");

    std::uint32_t absTick =
        static_cast<std::uint32_t>(tick < 0 ? -tick : tick);

    uint256 ratio = (absTick & 0x1)
        ? uint256("0xfffcb933bd6fad37aa2d162d1a594001")
        : uint256("0x100000000000000000000000000000000");

    auto mulShift = [&](uint256 const& factor) {
        ratio = (ratio * factor) >> 128;
    };

    if (absTick & 0x2)
        mulShift(uint256("0xfff97272373d413259a46990580e213a"));
    if (absTick & 0x4)
        mulShift(uint256("0xfff2e50f5f656932ef12357cf3c7fdcc"));
    if (absTick & 0x8)
        mulShift(uint256("0xffe5caca7e10e4e61c3624eaa0941cd0"));
    if (absTick & 0x10)
        mulShift(uint256("0xffcb9843d60f6159c9db58835c926644"));
    if (absTick & 0x20)
        mulShift(uint256("0xff973b41fa98c081472e6896dfb254c0"));
    if (absTick & 0x40)
        mulShift(uint256("0xff2ea16466c96a3843ec78b326b52861"));
    if (absTick & 0x80)
        mulShift(uint256("0xfe5dee046a99a2a811c461f1969c3053"));
    if (absTick & 0x100)
        mulShift(uint256("0xfcbe86c7900a88aedcffc83b479aa3a4"));
    if (absTick & 0x200)
        mulShift(uint256("0xf987a7253ac413176f2b074cf7815e54"));
    if (absTick & 0x400)
        mulShift(uint256("0xf3392b0822b70005940c7a398e4b70f3"));
    if (absTick & 0x800)
        mulShift(uint256("0xe7159475a2c29b7443b29c7fa6e889d9"));
    if (absTick & 0x1000)
        mulShift(uint256("0xd097f3bdfd2022b8845ad8f792aa5825"));
    if (absTick & 0x2000)
        mulShift(uint256("0xa9f746462d870fdf8a65dc1f90e061e5"));
    if (absTick & 0x4000)
        mulShift(uint256("0x70d869a156d2a1b890bb3df62baf32f7"));
    if (absTick & 0x8000)
        mulShift(uint256("0x31be135f97d08fd981231505542fcfa6"));
    if (absTick & 0x10000)
        mulShift(uint256("0x9aa508b5b7a84e1c677de54f3e99bc9"));
    if (absTick & 0x20000)
        mulShift(uint256("0x5d6af8dedb81196699c329225ee604"));
    if (absTick & 0x40000)
        mulShift(uint256("0x2216e584f5fa1ea926041bedfe98"));
    if (absTick & 0x80000)
        mulShift(uint256("0x48a170391f7dc42444e8fa2"));

    if (tick > 0)
    {
        static uint256 const maxUint256 = (uint256(1) << 256) - 1;
        ratio = maxUint256 / ratio;
    }

    // Convert from Q128.128 to Q64.96 by shifting right 32 bits
    uint128 result = static_cast<uint128>(
        (ratio >> 32) +
        (((ratio % (uint256(1) << 32)) == 0) ? uint256(0) : uint256(1)));

    return result;
}

std::int32_t
sqrtPriceToTick(uint128 const& sqrtPrice)
{
    XRPL_ASSERT(
        sqrtPrice >= minSqrtRatio() && sqrtPrice < maxSqrtRatio(),
        "xrpl::clamm::sqrtPriceToTick : price in range");

    // Defensive: clamp to valid range if assert is disabled
    if (sqrtPrice < minSqrtRatio())
        return CLAMM_MIN_TICK;
    if (sqrtPrice >= maxSqrtRatio())
        return CLAMM_MAX_TICK;

    // Binary search: find the largest tick t such that
    // tickToSqrtPrice(t) <= sqrtPrice.
    std::int32_t lo = CLAMM_MIN_TICK;
    std::int32_t hi = CLAMM_MAX_TICK;

    while (lo < hi)
    {
        // Use upper midpoint to converge correctly
        std::int32_t mid = lo + (hi - lo + 1) / 2;
        if (tickToSqrtPrice(mid) <= sqrtPrice)
            lo = mid;
        else
            hi = mid - 1;
    }

    return lo;
}

// ---- Liquidity Math ----

std::uint64_t
getAmount0ForLiquidity(
    uint128 const& sqrtPriceA,
    uint128 const& sqrtPriceB,
    uint128 const& liquidity)
{
    auto const [lower, upper] = std::minmax(sqrtPriceA, sqrtPriceB);
    if (lower == 0)
        return 0;

    // Use uint512 for intermediate to avoid overflow:
    // liquidity(128) * q96(96) * delta(160) can reach 384 bits.
    uint512 numerator =
        uint512(liquidity) * uint512(q96Scale()) * uint512(upper - lower);
    uint512 denominator = uint512(lower) * uint512(upper);

    if (denominator == 0)
        return 0;

    auto result = numerator / denominator;
    // SECURITY: Saturation to uint64::max is intentional. Large liquidity
    // amounts with extreme price ranges can produce values exceeding 64 bits.
    // Clamping matches Uniswap V3's getAmount0Delta behavior -- callers
    // handle the saturated value safely (deposit caps, swap step limits).
    if (result > std::numeric_limits<std::uint64_t>::max())
        return std::numeric_limits<std::uint64_t>::max();
    return static_cast<std::uint64_t>(result);
}

std::uint64_t
getAmount1ForLiquidity(
    uint128 const& sqrtPriceA,
    uint128 const& sqrtPriceB,
    uint128 const& liquidity)
{
    auto const [lower, upper] = std::minmax(sqrtPriceA, sqrtPriceB);

    uint256 result =
        uint256(liquidity) * uint256(upper - lower) / q96Scale();

    // SECURITY: Same saturation pattern as getAmount0ForLiquidity above.
    // Prevents overflow when converting uint256 intermediate to uint64 output.
    if (result > std::numeric_limits<std::uint64_t>::max())
        return std::numeric_limits<std::uint64_t>::max();
    return static_cast<std::uint64_t>(result);
}

uint128
getLiquidityForAmounts(
    uint128 const& sqrtPrice,
    uint128 const& sqrtPriceA,
    uint128 const& sqrtPriceB,
    std::uint64_t amount0,
    std::uint64_t amount1)
{
    auto const [lower, upper] = std::minmax(sqrtPriceA, sqrtPriceB);

    if (sqrtPrice <= lower)
    {
        if (upper == lower)
            return 0;
        uint256 num = uint256(amount0) * uint256(lower) * uint256(upper);
        uint256 den = uint256(q96Scale()) * uint256(upper - lower);
        return static_cast<uint128>(num / den);
    }
    else if (sqrtPrice < upper)
    {
        uint128 liq0 = [&]() {
            if (upper == sqrtPrice)
                return uint128(0);
            uint256 num = uint256(amount0) * uint256(sqrtPrice) * uint256(upper);
            uint256 den = uint256(q96Scale()) * uint256(upper - sqrtPrice);
            return static_cast<uint128>(num / den);
        }();

        uint128 liq1 = [&]() {
            if (sqrtPrice == lower)
                return uint128(0);
            uint256 num = uint256(amount1) * uint256(q96Scale());
            uint256 den = uint256(sqrtPrice - lower);
            return static_cast<uint128>(num / den);
        }();

        return std::min(liq0, liq1);
    }
    else
    {
        if (upper == lower)
            return 0;
        uint256 num = uint256(amount1) * uint256(q96Scale());
        uint256 den = uint256(upper - lower);
        return static_cast<uint128>(num / den);
    }
}

// ---- Swap Math ----

static uint128
getNextSqrtPriceFromAmount0RoundingUp(
    uint128 const& sqrtPrice,
    uint128 const& liquidity,
    std::uint64_t amount)
{
    if (amount == 0)
        return sqrtPrice;
    if (liquidity == 0)
        return sqrtPrice;

    // Use uint512 for intermediate: numerator1(224) * sqrtPrice(160) = 384 bits.
    uint512 numerator1 = uint512(liquidity) << Q96;
    uint512 product = uint512(amount) * uint512(sqrtPrice);
    uint512 denominator = numerator1 + product;
    if (denominator == 0)
        return sqrtPrice;
    return static_cast<uint128>(
        (numerator1 * uint512(sqrtPrice) + denominator - 1) / denominator);
}

static uint128
getNextSqrtPriceFromAmount1RoundingDown(
    uint128 const& sqrtPrice,
    uint128 const& liquidity,
    std::uint64_t amount)
{
    if (amount == 0 || liquidity == 0)
        return sqrtPrice;
    uint256 quotient = (uint256(amount) << Q96) / uint256(liquidity);
    uint256 sum = uint256(sqrtPrice) + quotient;
    static uint256 const maxUint128 = (uint256(1) << 128) - 1;
    if (sum > maxUint128)
        return static_cast<uint128>(maxUint128);
    return static_cast<uint128>(sum);
}

SwapStepResult
computeSwapStep(
    uint128 const& sqrtPriceCurrent,
    uint128 const& sqrtPriceTarget,
    uint128 const& liquidity,
    std::uint64_t amountRemaining,
    std::uint16_t feePpm,
    bool zeroForOne)
{
    SwapStepResult result{};

    constexpr std::uint32_t FEE_DENOMINATOR = 1000000;
    std::uint64_t amountRemainingLessFee = static_cast<std::uint64_t>(
        (uint256(amountRemaining) * (FEE_DENOMINATOR - feePpm)) /
        FEE_DENOMINATOR);

    std::uint64_t amountIn;
    if (zeroForOne)
    {
        amountIn = getAmount0ForLiquidity(
            sqrtPriceTarget, sqrtPriceCurrent, liquidity);
    }
    else
    {
        amountIn = getAmount1ForLiquidity(
            sqrtPriceCurrent, sqrtPriceTarget, liquidity);
    }

    if (amountRemainingLessFee >= amountIn)
    {
        result.sqrtPriceNext = sqrtPriceTarget;
    }
    else
    {
        if (zeroForOne)
        {
            result.sqrtPriceNext = getNextSqrtPriceFromAmount0RoundingUp(
                sqrtPriceCurrent, liquidity, amountRemainingLessFee);
        }
        else
        {
            result.sqrtPriceNext = getNextSqrtPriceFromAmount1RoundingDown(
                sqrtPriceCurrent, liquidity, amountRemainingLessFee);
        }
    }

    bool const reachedTarget = (result.sqrtPriceNext == sqrtPriceTarget);

    if (zeroForOne)
    {
        result.amountIn = reachedTarget
            ? amountIn
            : getAmount0ForLiquidity(
                  result.sqrtPriceNext, sqrtPriceCurrent, liquidity);
        result.amountOut = getAmount1ForLiquidity(
            result.sqrtPriceNext, sqrtPriceCurrent, liquidity);
    }
    else
    {
        result.amountIn = reachedTarget
            ? amountIn
            : getAmount1ForLiquidity(
                  sqrtPriceCurrent, result.sqrtPriceNext, liquidity);
        result.amountOut = getAmount0ForLiquidity(
            sqrtPriceCurrent, result.sqrtPriceNext, liquidity);
    }

    if (!reachedTarget)
    {
        result.feeAmount = amountRemaining - result.amountIn;
    }
    else
    {
        // Match Uniswap V3: fee = amountIn * feePpm / (FEE_DENOMINATOR - feePpm)
        // This ensures amountIn + feeAmount <= amountRemaining (no underflow).
        auto const feeDenom = FEE_DENOMINATOR - feePpm;
        result.feeAmount = feeDenom > 0
            ? static_cast<std::uint64_t>(
                  (uint256(result.amountIn) * feePpm + feeDenom - 1) /
                  feeDenom)
            : amountRemaining - result.amountIn;
    }

    return result;
}

// ---- Tick Bitmap ----

std::pair<std::int16_t, std::uint8_t>
tickBitmapPosition(std::int32_t tick, std::uint16_t tickSpacing)
{
    std::int32_t compressed = tick / static_cast<std::int32_t>(tickSpacing);
    // C++ integer division truncates toward zero, but we need floor division
    if (tick < 0 && (tick % static_cast<std::int32_t>(tickSpacing)) != 0)
        --compressed;

    auto const wordPos = static_cast<std::int16_t>(compressed >> 8);
    auto const bitPos =
        static_cast<std::uint8_t>(compressed & 0xFF);
    return {wordPos, bitPos};
}

// base_uint<256> bit manipulation helpers.
// base_uint stores data big-endian: data()[0] is MSB.
// Bit 0 = least significant bit = last byte, lowest bit.
namespace {

bool
getBit256(base_uint<256> const& v, int bit)
{
    // bit 0 is LSB. Byte index from end: bit/8. Within byte: bit%8.
    auto const byteIdx = 31 - (bit / 8);
    auto const bitInByte = bit % 8;
    return (v.data()[byteIdx] >> bitInByte) & 1;
}

base_uint<256>
makeBitMask256(int bit)
{
    base_uint<256> mask{};
    auto const byteIdx = 31 - (bit / 8);
    auto const bitInByte = bit % 8;
    // data() returns unsigned char*
    const_cast<unsigned char*>(mask.data())[byteIdx] =
        static_cast<unsigned char>(1u << bitInByte);
    return mask;
}

}  // namespace

TER
flipTickBitmap(
    ApplyView& view,
    base_uint<256> const& poolID,
    AccountID const& poolAccount,
    std::int32_t tick,
    std::uint16_t tickSpacing,
    beast::Journal j)
{
    auto const [wordPos, bitPos] = tickBitmapPosition(tick, tickSpacing);
    auto const bitmapKeylet = keylet::clammTickBitmap(poolID, wordPos);

    auto const mask = makeBitMask256(bitPos);

    auto sleBitmap = view.peek(bitmapKeylet);
    if (!sleBitmap)
    {
        sleBitmap = std::make_shared<SLE>(bitmapKeylet);
        sleBitmap->setFieldH256(sfPoolID, poolID);
        sleBitmap->setFieldI32(sfTickIndex, static_cast<std::int32_t>(wordPos));
        sleBitmap->setFieldH256(sfDigest, mask);

        auto page = view.dirInsert(
            keylet::ownerDir(poolAccount),
            bitmapKeylet,
            describeOwnerDir(poolAccount));
        if (!page)
            return tecDIR_FULL;
        sleBitmap->setFieldU64(sfOwnerNode, *page);
        sleBitmap->setFieldH256(sfPreviousTxnID, base_uint<256>{});
        sleBitmap->setFieldU32(sfPreviousTxnLgrSeq, 0);
        view.insert(sleBitmap);
    }
    else
    {
        auto digest = sleBitmap->getFieldH256(sfDigest);
        digest ^= mask;

        if (digest == beast::zero)
        {
            auto const ownerNode = sleBitmap->getFieldU64(sfOwnerNode);
            view.dirRemove(
                keylet::ownerDir(poolAccount),
                ownerNode,
                bitmapKeylet,
                true);
            view.erase(sleBitmap);
        }
        else
        {
            sleBitmap->setFieldH256(sfDigest, digest);
            view.update(sleBitmap);
        }
    }
    return tesSUCCESS;
}

std::optional<std::pair<std::int32_t, uint128>>
findNextInitializedTickBitmap(
    ReadView const& view,
    base_uint<256> const& poolID,
    std::int32_t currentTick,
    std::uint16_t tickSpacing,
    bool zeroForOne)
{
    auto const step = static_cast<std::int32_t>(tickSpacing);

    // Compute the compressed tick position
    std::int32_t compressed;
    if (zeroForOne)
    {
        std::int32_t aligned;
        if (currentTick >= 0)
            aligned = (currentTick / step) * step;
        else
            aligned = ((currentTick - step + 1) / step) * step;
        // Include current aligned tick in the search (Uniswap V3: lte
        // direction includes the current position).  Previously this
        // was `aligned / step - 1`, which skipped the current tick and
        // prevented crossing when the price was exactly at a boundary.
        compressed = aligned / step;
        if (aligned < 0 && (aligned % step) != 0)
            --compressed;
    }
    else
    {
        std::int32_t aligned;
        if (currentTick >= 0)
            aligned = ((currentTick / step) + 1) * step;
        else
            aligned = ((currentTick + 1) / step) * step;
        compressed = aligned / step;
        if (aligned < 0 && (aligned % step) != 0)
            --compressed;
    }

    constexpr int maxWordSearches = 8;

    if (zeroForOne)
    {
        auto wordPos = static_cast<std::int16_t>(compressed >> 8);
        auto bitPos = static_cast<std::uint8_t>(compressed & 0xFF);

        for (int w = 0; w < maxWordSearches; ++w)
        {
            auto const sleBitmap =
                view.read(keylet::clammTickBitmap(poolID, wordPos));
            if (sleBitmap)
            {
                auto const digest = sleBitmap->getFieldH256(sfDigest);
                // Find highest set bit at position <= bitPos
                for (int b = bitPos; b >= 0; --b)
                {
                    if (getBit256(digest, b))
                    {
                        auto const t =
                            (static_cast<std::int32_t>(wordPos) * 256 + b) *
                            step;
                        if (t >= CLAMM_MIN_TICK)
                            return std::make_pair(t, tickToSqrtPrice(t));
                    }
                }
            }
            --wordPos;
            bitPos = 255;
        }
    }
    else
    {
        auto wordPos = static_cast<std::int16_t>(compressed >> 8);
        auto bitPos = static_cast<std::uint8_t>(compressed & 0xFF);

        for (int w = 0; w < maxWordSearches; ++w)
        {
            auto const sleBitmap =
                view.read(keylet::clammTickBitmap(poolID, wordPos));
            if (sleBitmap)
            {
                auto const digest = sleBitmap->getFieldH256(sfDigest);
                // Find lowest set bit at position >= bitPos
                for (int b = bitPos; b < 256; ++b)
                {
                    if (getBit256(digest, b))
                    {
                        auto const t =
                            (static_cast<std::int32_t>(wordPos) * 256 + b) *
                            step;
                        if (t <= CLAMM_MAX_TICK)
                            return std::make_pair(t, tickToSqrtPrice(t));
                    }
                }
            }
            ++wordPos;
            bitPos = 0;
        }
    }

    return std::nullopt;
}

// ---- Tick Scanning ----

std::optional<std::pair<std::int32_t, uint128>>
findNextInitializedTick(
    ReadView const& view,
    base_uint<256> const& poolID,
    std::int32_t currentTick,
    std::uint16_t tickSpacing,
    bool zeroForOne)
{
    // Try bitmap lookup first (fast path)
    auto result =
        findNextInitializedTickBitmap(view, poolID, currentTick, tickSpacing, zeroForOne);
    if (result)
        return result;

    // Fallback to linear scan if bitmap SLEs don't exist yet
    constexpr int maxSearchSteps = 256;
    auto const step = static_cast<std::int32_t>(tickSpacing);

    auto tick = currentTick;
    if (zeroForOne)
    {
        if (tick >= 0)
            tick = (tick / step) * step;
        else
            tick = ((tick - step + 1) / step) * step;

        // Check the aligned tick itself first (Uniswap V3: search
        // includes current position for zeroForOne / lte direction).
        // This is critical when the price has reached a tick boundary
        // but hasn't crossed it yet.
        if (tick >= CLAMM_MIN_TICK &&
            view.read(keylet::clammTick(poolID, tick)))
            return std::make_pair(tick, tickToSqrtPrice(tick));

        for (int i = 0; i < maxSearchSteps; ++i)
        {
            tick -= step;
            if (tick < CLAMM_MIN_TICK)
                break;
            if (view.read(keylet::clammTick(poolID, tick)))
                return std::make_pair(tick, tickToSqrtPrice(tick));
        }
    }
    else
    {
        if (tick >= 0)
            tick = ((tick / step) + 1) * step;
        else
            tick = ((tick + 1) / step) * step;

        for (int i = 0; i < maxSearchSteps; ++i)
        {
            if (tick > CLAMM_MAX_TICK)
                break;
            if (view.read(keylet::clammTick(poolID, tick)))
                return std::make_pair(tick, tickToSqrtPrice(tick));
            tick += step;
        }
    }

    return std::nullopt;
}

// ---- Swap Simulation ----

SwapSimulation
simulateSwap(
    ReadView const& view,
    base_uint<256> const& poolID,
    uint128 sqrtPrice,
    std::int32_t currentTick,
    uint128 liquidity,
    std::uint16_t tickSpacing,
    std::uint16_t tradingFee,
    std::uint64_t amountIn,
    bool zeroForOne)
{
    SwapSimulation sim;
    sim.finalSqrtPrice = sqrtPrice;
    sim.finalTick = currentTick;
    sim.finalLiquidity = liquidity;

    auto amountRemaining = amountIn;

    constexpr int maxIterations = 100;
    for (int i = 0; i < maxIterations && amountRemaining > 0; ++i)
    {
        uint128 sqrtPriceTarget;
        std::optional<std::int32_t> nextTick;

        auto const found = findNextInitializedTick(
            view, poolID, sim.finalTick, tickSpacing, zeroForOne);

        if (found)
        {
            nextTick = found->first;
            sqrtPriceTarget = found->second;
        }
        else
        {
            sqrtPriceTarget =
                zeroForOne ? minSqrtRatio() : maxSqrtRatio();
        }

        if (sim.finalLiquidity == 0)
        {
            if (nextTick)
            {
                sim.finalSqrtPrice = sqrtPriceTarget;
                sim.finalTick = *nextTick;
                if (auto sleTick =
                        view.read(keylet::clammTick(poolID, *nextTick)))
                {
                    auto const liqNet = fromSLEFieldSigned(
                        sleTick->getFieldH128(sfLiquidityNet));
                    int128 signedLiq =
                        static_cast<int128>(sim.finalLiquidity);
                    if (zeroForOne)
                        signedLiq -= liqNet;
                    else
                        signedLiq += liqNet;
                    sim.finalLiquidity = (signedLiq < 0)
                        ? uint128(0)
                        : static_cast<uint128>(signedLiq);
                }
                continue;
            }
            else
            {
                break;
            }
        }

        auto const step = computeSwapStep(
            sim.finalSqrtPrice,
            sqrtPriceTarget,
            sim.finalLiquidity,
            amountRemaining,
            tradingFee,
            zeroForOne);

        sim.finalSqrtPrice = step.sqrtPriceNext;
        auto const consumed = std::min(
            step.amountIn + step.feeAmount, amountRemaining);
        amountRemaining -= consumed;
        sim.amountOut += step.amountOut;
        sim.feeAmount += step.feeAmount;

        if (nextTick && sim.finalSqrtPrice == sqrtPriceTarget)
        {
            if (auto sleTick =
                    view.read(keylet::clammTick(poolID, *nextTick)))
            {
                auto const liqNet = fromSLEFieldSigned(
                    sleTick->getFieldH128(sfLiquidityNet));
                int128 signedLiq =
                    static_cast<int128>(sim.finalLiquidity);
                if (zeroForOne)
                    signedLiq -= liqNet;
                else
                    signedLiq += liqNet;
                sim.finalLiquidity = (signedLiq < 0)
                    ? uint128(0)
                    : static_cast<uint128>(signedLiq);
            }
            ++sim.ticksCrossed;
            sim.finalTick =
                zeroForOne ? (*nextTick - 1) : *nextTick;
        }
        else
        {
            sim.finalTick = sqrtPriceToTick(sim.finalSqrtPrice);
            break;
        }
    }

    sim.amountIn = amountIn - amountRemaining;
    return sim;
}

// ---- Apply Swap ----

SwapResult
applySwap(
    ApplyView& view,
    base_uint<256> const& poolID,
    uint128 sqrtPrice,
    std::int32_t currentTick,
    uint128 liquidity,
    std::uint16_t tickSpacing,
    std::uint16_t tradingFee,
    uint128 feeGrowthGlobal0,
    uint128 feeGrowthGlobal1,
    std::uint64_t amountIn,
    bool zeroForOne,
    uint128 sqrtPriceLimit,
    beast::Journal j)
{
    SwapResult result;
    result.feeGrowthGlobal0 = feeGrowthGlobal0;
    result.feeGrowthGlobal1 = feeGrowthGlobal1;
    result.finalSqrtPrice = sqrtPrice;
    result.finalTick = currentTick;
    result.finalLiquidity = liquidity;

    auto amountRemaining = amountIn;

    constexpr int maxIterations = 100;
    for (int i = 0; i < maxIterations && amountRemaining > 0; ++i)
    {
        // Check price limit
        if (zeroForOne && result.finalSqrtPrice <= sqrtPriceLimit)
            break;
        if (!zeroForOne && result.finalSqrtPrice >= sqrtPriceLimit)
            break;

        uint128 sqrtPriceTarget;
        std::optional<std::int32_t> nextTick;

        auto const found = findNextInitializedTick(
            view, poolID, result.finalTick, tickSpacing, zeroForOne);

        if (found)
        {
            nextTick = found->first;
            sqrtPriceTarget = found->second;
        }
        else
        {
            sqrtPriceTarget =
                zeroForOne ? minSqrtRatio() : maxSqrtRatio();
        }

        // Clamp target to price limit
        if (zeroForOne && sqrtPriceTarget < sqrtPriceLimit)
            sqrtPriceTarget = sqrtPriceLimit;
        if (!zeroForOne && sqrtPriceTarget > sqrtPriceLimit)
            sqrtPriceTarget = sqrtPriceLimit;

        if (result.finalLiquidity == 0)
        {
            if (nextTick)
            {
                result.finalSqrtPrice = sqrtPriceTarget;
                result.finalTick = *nextTick;
                auto const tickKeylet =
                    keylet::clammTick(poolID, *nextTick);
                if (auto sleTick = view.peek(tickKeylet))
                {
                    auto const liqNet = fromSLEFieldSigned(
                        sleTick->getFieldH128(sfLiquidityNet));
                    int128 signedLiq =
                        static_cast<int128>(result.finalLiquidity);
                    if (zeroForOne)
                        signedLiq -= liqNet;
                    else
                        signedLiq += liqNet;
                    result.finalLiquidity = (signedLiq < 0)
                        ? uint128(0)
                        : static_cast<uint128>(signedLiq);
                }
                continue;
            }
            else
            {
                JLOG(j.debug()) << "CLAMM applySwap: no liquidity.";
                break;
            }
        }

        auto const step = computeSwapStep(
            result.finalSqrtPrice,
            sqrtPriceTarget,
            result.finalLiquidity,
            amountRemaining,
            tradingFee,
            zeroForOne);

        result.finalSqrtPrice = step.sqrtPriceNext;
        auto const consumed = std::min(
            step.amountIn + step.feeAmount, amountRemaining);
        amountRemaining -= consumed;
        result.amountOut += step.amountOut;
        result.totalFees += step.feeAmount;

        // Accumulate fee growth
        if (result.finalLiquidity > 0 && step.feeAmount > 0)
        {
            uint256 feeGrowth =
                (uint256(step.feeAmount) << Q96) /
                uint256(result.finalLiquidity);
            if (zeroForOne)
                result.feeGrowthGlobal0 +=
                    static_cast<uint128>(feeGrowth);
            else
                result.feeGrowthGlobal1 +=
                    static_cast<uint128>(feeGrowth);
        }

        // Cross tick if we reached the boundary
        if (nextTick && result.finalSqrtPrice == sqrtPriceTarget &&
            sqrtPriceTarget != sqrtPriceLimit)
        {
            auto const tickKeylet =
                keylet::clammTick(poolID, *nextTick);
            auto sleTick = view.peek(tickKeylet);
            if (sleTick)
            {
                // Flip fee growth outside for the crossed tick
                auto fgo0 = fromSLEField(
                    sleTick->getFieldH128(sfFeeGrowthOutside0));
                auto fgo1 = fromSLEField(
                    sleTick->getFieldH128(sfFeeGrowthOutside1));
                fgo0 = result.feeGrowthGlobal0 - fgo0;
                fgo1 = result.feeGrowthGlobal1 - fgo1;
                sleTick->setFieldH128(
                    sfFeeGrowthOutside0, toSLEField(fgo0));
                sleTick->setFieldH128(
                    sfFeeGrowthOutside1, toSLEField(fgo1));
                view.update(sleTick);

                // Update active liquidity
                auto const liqNet = fromSLEFieldSigned(
                    sleTick->getFieldH128(sfLiquidityNet));
                {
                    int128 signedLiq =
                        static_cast<int128>(result.finalLiquidity);
                    if (zeroForOne)
                        signedLiq -= liqNet;
                    else
                        signedLiq += liqNet;
                    result.finalLiquidity = (signedLiq < 0)
                        ? uint128(0)
                        : static_cast<uint128>(signedLiq);
                }
            }

            result.finalTick =
                zeroForOne ? (*nextTick - 1) : *nextTick;
        }
        else
        {
            result.finalTick = sqrtPriceToTick(result.finalSqrtPrice);
            break;
        }
    }

    result.amountIn = amountIn - amountRemaining;
    return result;
}

// ---- STAmount Conversion ----

std::uint64_t
extractAmount(STAmount const& amt)
{
    if (amt.native())
        return static_cast<std::uint64_t>(amt.xrp().drops());

    // Convert IOU to drops-equivalent (6 decimal places).
    // IOU value = mantissa * 10^exponent
    // drops-equiv = value * 10^6 = mantissa * 10^(exponent + 6)
    auto const mantissa = amt.mantissa();
    auto const exponent = amt.exponent();
    int const e = exponent + 6;

    // SECURITY: Loop cap (i < 20) is safe because STAmount normalizes IOU
    // mantissa/exponent such that exponent is bounded to [-96, 80]. With the
    // +6 offset, e ranges from [-90, 86], but multiplication overflow is
    // caught by the pre-check before each *=10 step.
    if (e >= 0)
    {
        std::uint64_t result = mantissa;
        for (int i = 0; i < e && i < 20; ++i)
        {
            if (result > std::numeric_limits<std::uint64_t>::max() / 10)
                return std::numeric_limits<std::uint64_t>::max();
            result *= 10;
        }
        return result;
    }
    else
    {
        // Cap at 19 iterations: 10^19 is the largest power of 10 that
        // fits in uint64_t.  Since STAmount mantissa <= ~10^16,
        // mantissa / 10^19 is already 0 for any valid IOU.
        std::uint64_t divisor = 1;
        for (int i = 0; i < -e && i < 19; ++i)
            divisor *= 10;
        return mantissa / divisor;
    }
}

STAmount
makeSTAmount(Issue const& issue, std::uint64_t amount)
{
    if (isXRP(issue))
    {
        // Guard against uint64 -> int64 overflow (theoretical limit)
        auto const capped = std::min(
            amount,
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()));
        return STAmount(XRPAmount(static_cast<std::int64_t>(capped)));
    }

    if (amount == 0)
        return STAmount(issue);

    // amount is in drops-equivalent (10^-6 units)
    return STAmount(issue, amount, -6);
}

// ---- Fee Growth Inside ----
// SECURITY: All subtractions in this function (feeGrowthGlobal - fgo,
// and the final feeGrowthGlobal - below - above) use unsigned uint128
// wrapping arithmetic intentionally. This is the same modular arithmetic
// pattern as Uniswap V3's getFeeGrowthInside. The values wrap around on
// underflow, and the difference remains correct as long as the global
// counter has not wrapped more than once relative to a position's
// last-collected snapshot -- which is guaranteed because fee growth
// cannot exceed 2^128 per position lifetime.

FeeGrowthInside
computeFeeGrowthInside(
    ReadView const& view,
    base_uint<256> const& poolID,
    std::int32_t lowerTick,
    std::int32_t upperTick,
    std::int32_t currentTick,
    uint128 const& feeGrowthGlobal0,
    uint128 const& feeGrowthGlobal1)
{
    // Compute feeGrowthBelow from lower tick
    uint128 feeGrowthBelow0 = 0;
    uint128 feeGrowthBelow1 = 0;
    if (auto const sleLower =
            view.read(keylet::clammTick(poolID, lowerTick)))
    {
        auto const fgo0 = fromSLEField(sleLower->getFieldH128(sfFeeGrowthOutside0));
        auto const fgo1 = fromSLEField(sleLower->getFieldH128(sfFeeGrowthOutside1));
        if (currentTick >= lowerTick)
        {
            feeGrowthBelow0 = fgo0;
            feeGrowthBelow1 = fgo1;
        }
        else
        {
            feeGrowthBelow0 = feeGrowthGlobal0 - fgo0;
            feeGrowthBelow1 = feeGrowthGlobal1 - fgo1;
        }
    }

    // Compute feeGrowthAbove from upper tick
    uint128 feeGrowthAbove0 = 0;
    uint128 feeGrowthAbove1 = 0;
    if (auto const sleUpper =
            view.read(keylet::clammTick(poolID, upperTick)))
    {
        auto const fgo0 = fromSLEField(sleUpper->getFieldH128(sfFeeGrowthOutside0));
        auto const fgo1 = fromSLEField(sleUpper->getFieldH128(sfFeeGrowthOutside1));
        if (currentTick < upperTick)
        {
            feeGrowthAbove0 = fgo0;
            feeGrowthAbove1 = fgo1;
        }
        else
        {
            feeGrowthAbove0 = feeGrowthGlobal0 - fgo0;
            feeGrowthAbove1 = feeGrowthGlobal1 - fgo1;
        }
    }

    return FeeGrowthInside{
        feeGrowthGlobal0 - feeGrowthBelow0 - feeGrowthAbove0,
        feeGrowthGlobal1 - feeGrowthBelow1 - feeGrowthAbove1};
}

}  // namespace clamm

std::optional<uint256>
resolvePoolID(STTx const& tx)
{
    if (tx.isFieldPresent(sfPoolID))
        return tx.getFieldH256(sfPoolID);

    if (tx.isFieldPresent(sfAsset) &&
        tx.isFieldPresent(sfAsset2) &&
        tx.isFieldPresent(sfFeeTier))
    {
        auto const asset = tx[sfAsset].get<Issue>();
        auto const asset2 = tx[sfAsset2].get<Issue>();
        auto const feeTier = tx[sfFeeTier];
        return keylet::clamm(asset, asset2, feeTier).key;
    }

    return std::nullopt;
}

}  // namespace xrpl
