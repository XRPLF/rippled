#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <boost/multiprecision/cpp_int.hpp>

#include <cstdint>

namespace xrpl {
namespace clamm {

// Type aliases for Q64.96 fixed-point arithmetic.
using uint128 = boost::multiprecision::uint128_t;
using uint256 = boost::multiprecision::uint256_t;
using uint512 = boost::multiprecision::uint512_t;
using int128 = boost::multiprecision::int128_t;
using int256 = boost::multiprecision::int256_t;

// Q64.96 shift amount
constexpr int Q96 = 96;

// Q64.96 scale factor: 2^96
inline uint128 const&
q96Scale()
{
    static uint128 const val = uint128(1) << Q96;
    return val;
}

// Minimum and maximum sqrt ratios (from Uniswap V3 TickMath.sol)
inline uint128 const&
minSqrtRatio()
{
    static uint128 const val("4295128739");
    return val;
}

inline uint128 const&
maxSqrtRatio()
{
    static uint128 const val(
        "1461446703485210103287273052203988822378723970342");
    return val;
}

/** Result of a single swap step computation */
struct SwapStepResult
{
    uint128 sqrtPriceNext;
    uint64_t amountIn;
    uint64_t amountOut;
    uint64_t feeAmount;
};

// ---- Tick Math ----

uint128
tickToSqrtPrice(std::int32_t tick);

std::int32_t
sqrtPriceToTick(uint128 const& sqrtPrice);

// ---- Liquidity Math ----

std::uint64_t
getAmount0ForLiquidity(
    uint128 const& sqrtPriceA,
    uint128 const& sqrtPriceB,
    uint128 const& liquidity);

std::uint64_t
getAmount1ForLiquidity(
    uint128 const& sqrtPriceA,
    uint128 const& sqrtPriceB,
    uint128 const& liquidity);

uint128
getLiquidityForAmounts(
    uint128 const& sqrtPrice,
    uint128 const& sqrtPriceA,
    uint128 const& sqrtPriceB,
    std::uint64_t amount0,
    std::uint64_t amount1);

// ---- Swap Math ----

SwapStepResult
computeSwapStep(
    uint128 const& sqrtPriceCurrent,
    uint128 const& sqrtPriceTarget,
    uint128 const& liquidity,
    std::uint64_t amountRemaining,
    std::uint16_t feePpm,
    bool zeroForOne);

// ---- SLE field conversion ----

uint128
fromSLEField(base_uint<128> const& field);

base_uint<128>
toSLEField(uint128 const& value);

int128
fromSLEFieldSigned(base_uint<128> const& field);

base_uint<128>
toSLEFieldSigned(int128 const& value);

// ---- Tick Bitmap ----

/** Compute the word position and bit position for a tick in the bitmap.
 *  The tick is first compressed by dividing by tickSpacing.
 *  Word position = compressed >> 8, bit position = compressed & 0xFF.
 */
std::pair<std::int16_t, std::uint8_t>
tickBitmapPosition(std::int32_t tick, std::uint16_t tickSpacing);

/** Toggle a bit in the tick bitmap when a tick is initialized or cleared.
 *  Creates or deletes bitmap SLEs as needed.
 */
TER
flipTickBitmap(
    ApplyView& view,
    base_uint<256> const& poolID,
    AccountID const& poolAccount,
    std::int32_t tick,
    std::uint16_t tickSpacing,
    beast::Journal j);

/** Bitmap-based next initialized tick lookup.
 *  Returns the next initialized tick and its sqrt price, or nullopt.
 */
std::optional<std::pair<std::int32_t, uint128>>
findNextInitializedTickBitmap(
    ReadView const& view,
    base_uint<256> const& poolID,
    std::int32_t currentTick,
    std::uint16_t tickSpacing,
    bool zeroForOne);

// ---- Tick Scanning ----

/** Find the next initialized tick in the given direction.
 *  In zeroForOne direction: search downward (lower ticks).
 *  In oneForZero direction: search upward (higher ticks).
 *  Returns the tick index and its sqrt price, or nullopt if none found.
 */
std::optional<std::pair<std::int32_t, uint128>>
findNextInitializedTick(
    ReadView const& view,
    base_uint<256> const& poolID,
    std::int32_t currentTick,
    std::uint16_t tickSpacing,
    bool zeroForOne);

// ---- Swap Simulation ----

/** Result of a read-only swap simulation through a CLAMM pool. */
struct SwapSimulation
{
    std::uint64_t amountIn{0};    // total consumed input
    std::uint64_t amountOut{0};   // total output
    std::uint64_t feeAmount{0};   // total fees
    uint128 finalSqrtPrice;       // price after swap
    std::int32_t finalTick{0};    // tick after swap
    uint128 finalLiquidity;       // active liquidity after swap
    std::uint32_t ticksCrossed{0}; // number of initialized ticks crossed
};

/** Read-only swap simulation through a CLAMM pool.
 *  Iterates through ticks, computing swap steps and crossing ticks
 *  without modifying any ledger state.
 */
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
    bool zeroForOne);

// ---- Fee Growth Inside ----

/** Compute the fee growth accumulated inside a position's tick range.
 *  Uses the Uniswap V3 formula:
 *    feeGrowthInside = feeGrowthGlobal - feeGrowthBelow - feeGrowthAbove
 *  where "below" and "above" are derived from the tick's feeGrowthOutside
 *  values, flipped based on whether the tick is above or below current.
 */
struct FeeGrowthInside
{
    uint128 feeGrowthInside0;
    uint128 feeGrowthInside1;
};

FeeGrowthInside
computeFeeGrowthInside(
    ReadView const& view,
    base_uint<256> const& poolID,
    std::int32_t lowerTick,
    std::int32_t upperTick,
    std::int32_t currentTick,
    uint128 const& feeGrowthGlobal0,
    uint128 const& feeGrowthGlobal1);

// ---- Apply Swap ----

/** Result of applying a swap through a CLAMM pool.
 *  Unlike SwapSimulation, applySwap mutates tick SLEs (flips feeGrowthOutside
 *  on tick crossing) and tracks fee growth globals.
 */
struct SwapResult
{
    std::uint64_t amountIn{0};
    std::uint64_t amountOut{0};
    std::uint64_t totalFees{0};
    uint128 finalSqrtPrice;
    std::int32_t finalTick{0};
    uint128 finalLiquidity;
    uint128 feeGrowthGlobal0;
    uint128 feeGrowthGlobal1;
};

/** Execute a swap through a CLAMM pool, mutating tick state on crossing.
 *  This is the core swap loop used by both CLAMMSwap transactor and
 *  CLAMMOffer::consume().
 */
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
    beast::Journal j);

// ---- STAmount Conversion ----

/** Extract a uint64 "drops-equivalent" amount from an STAmount.
 *  XRP: returns drops directly.
 *  IOU: normalizes to 6 decimal places (same scale as drops).
 */
std::uint64_t
extractAmount(STAmount const& amt);

/** Construct an STAmount from a uint64 "drops-equivalent" amount.
 *  XRP: constructs native XRP amount in drops.
 *  IOU: constructs IOU with exponent -6.
 */
STAmount
makeSTAmount(Issue const& issue, std::uint64_t amount);

}  // namespace clamm

/** Resolve pool ID from transaction fields.
 *  Returns sfPoolID if present, or computes it from sfAsset + sfAsset2 + sfFeeTier.
 *  Returns nullopt if neither mode provides sufficient fields.
 */
std::optional<uint256>
resolvePoolID(STTx const& tx);

}  // namespace xrpl
