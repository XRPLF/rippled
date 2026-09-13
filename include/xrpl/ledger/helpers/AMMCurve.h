// Pluggable AMM curve architecture.
// Concentrated liquidity (CurveType 1) based on XRPL-Standards Discussion #427
// by Roman Thpt (@RomThpt), which adapted Uniswap v3 tick math, fee tier
// structure, and fee accounting to the XRPL. This implementation extends that
// work with a pluggable curve interface, StableSwap, and Smart AMM.
// See: https://github.com/XRPLF/XRPL-Standards/discussions/427

#pragma once

#include <expected>
#include <xrpl/basics/Number.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {

class ReadView;
class ApplyView;

struct CurveContext
{
    ReadView const* view = nullptr;
    uint256 const* ammID = nullptr;
    // Optional out: set to true if a CL swap-walk terminated because it hit
    // maxTickCrossings. Callers pass a pointer when they want to detect
    // the cap (e.g. to emit tecAMM_TICK_CAP_HIT); pass nullptr to ignore.
    bool* tickCapHit = nullptr;
};

class CurveInterface
{
public:
    virtual ~CurveInterface() = default;

    virtual std::expected<STAmount, TER>
    swapIn(
        STAmount const& poolIn,
        STAmount const& poolOut,
        STAmount const& assetIn,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& ctx = {}) const = 0;

    virtual std::expected<STAmount, TER>
    swapOut(
        STAmount const& poolIn,
        STAmount const& poolOut,
        STAmount const& assetOut,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& ctx = {}) const = 0;

    virtual std::expected<Number, TER>
    spotPrice(
        STAmount const& poolIn,
        STAmount const& poolOut,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& ctx = {}) const = 0;

    [[nodiscard]] virtual TER
    validateParams(STObject const& tx) const = 0;

    virtual std::expected<STAmount, TER>
    initialLPTokens(
        STAmount const& asset1,
        STAmount const& asset2,
        Issue const& lptIssue,
        STObject const* txParams) const = 0;

    virtual bool
    checkInvariant(
        STAmount const& oldIn,
        STAmount const& oldOut,
        STAmount const& newIn,
        STAmount const& newOut,
        STObject const* ammSle) const = 0;

    // Apply a realized swap to the AMM SLE. Trustline balances are updated
    // by the caller (BookStep). For CP and StableSwap the pool state is fully
    // implicit in trustline balances, so the default is a no-op. CL must
    // mutate currentTick/activeLiquidity/feeGrowthGlobal and flip
    // feeGrowthOutside on crossed ticks, because none of that state is
    // derivable from the trustlines alone.
    virtual TER
    applySwap(
        ApplyView& /*view*/,
        uint256 const& /*ammID*/,
        STAmount const& /*assetIn*/,
        STAmount const& /*assetOut*/,
        std::uint16_t /*tfee*/,
        STObject const* /*curveParams*/) const
    {
        return tesSUCCESS;
    }
};

CurveInterface const*
getCurve(std::uint8_t curveType, Rules const& rules);

// Max output the pool can deliver before crossing the next initialised
// tick boundary in the swap direction. Audit #19: caller (AMMLiquidity's
// offer generation) uses this to cap an advertised AMM offer at the
// current tick range, so the offer's quality reflects only the marginal
// range — not a blended average across multiple tick crossings.
//
// Returns std::nullopt if there is no further tick in the swap direction
// (no cap; offer is bounded only by reserves) or if the pool has no
// active liquidity. Returns 0 if the swap is already at the boundary.
// Caller should treat nullopt as "no cap".
std::optional<Number>
maxClOutputWithinCurrentRange(
    ReadView const& view,
    uint256 const& ammID,
    STObject const& ammSle,
    bool zeroForOne);

// Equivalent for CtBinned: cap the advertised AMMOffer output at the
// active bin's reserve. Without this cap, AMMLiquidity quotes against
// the pool's aggregate balance — which spans multiple bins at different
// prices — and BookStep mispricesthe offer's quality vs CLOB. Capping
// per active bin lets BookStep iterate naturally, getting each bin's
// marginal price one offer at a time.
//
// Returns std::nullopt if no active bin exists (empty pool) or the
// active bin lacks the output asset.
std::optional<Number>
maxBinnedOutputAtActiveBin(
    ReadView const& view,
    uint256 const& ammID,
    STObject const& ammSle,
    bool inIsAsset0);

// ─── CL tick bitmap ─────────────────────────────────────────────────────
//
// Sparse 256-tick-per-word presence bitmap. See spec §3.6.

// Convert a tick to its (wordIndex, bitInWord) position. Offset-binary so
// arithmetic stays in unsigned domain. Uses kTickBitmapOffset (= -minTick)
// from AMMCore.h — single source of truth, do NOT duplicate inline.
inline std::pair<std::uint16_t, std::uint8_t>
tickToBitmapPos(std::int32_t tick) noexcept
{
    auto const offsetT = static_cast<std::uint32_t>(
        tick + static_cast<std::int32_t>(kTickBitmapOffset));
    return {static_cast<std::uint16_t>(offsetT >> 8),
            static_cast<std::uint8_t>(offsetT & 0xFFu)};
}

inline std::int32_t
bitmapPosToTick(std::uint16_t wordIndex, std::uint8_t bitInWord) noexcept
{
    auto const offsetT =
        (static_cast<std::uint32_t>(wordIndex) << 8) | bitInWord;
    return static_cast<std::int32_t>(offsetT) -
        static_cast<std::int32_t>(kTickBitmapOffset);
}

// Bit-test for the bitmap word storage. `bits` is the raw `sfBitmapBits`
// value read off the SLE. Convention: bit i = LSB of byte (i/8), little-
// endian within bytes. The convention is internal — callers that write
// bits must use the same scheme (and do, via the maintenance helpers).
inline bool
bitmapBitIsSet(uint256 const& bits, std::uint8_t pos) noexcept
{
    return ((bits.data()[pos / 8]) >> (pos % 8)) & 1u;
}

// AMMDeposit and AMMWithdraw call these when a tick crosses the
// "initialised / uninitialised" boundary (sfLiquidityGross transitioning
// 0↔>0). Each pool has a sparse set of `ltAMM_TICK_BITMAP` SLEs covering
// 256 ticks each; setting / clearing creates and deletes those SLEs on
// demand. Idempotent — calling set on an already-set bit is a no-op.
//
// Returns tesSUCCESS on the happy path. The current implementation has
// no failure path beyond the AMM SLE being missing; reserved as TER for
// forward compatibility.
TER
setTickBitmap(ApplyView& view, uint256 const& ammID, std::int32_t tick, beast::Journal j);

TER
clearTickBitmap(ApplyView& view, uint256 const& ammID, std::int32_t tick, beast::Journal j);

inline std::uint8_t
getCurveType(SLE const& ammSle)
{
    if (ammSle.isFieldPresent(sfCurveType))
        return ammSle.getFieldU8(sfCurveType);
    return CtConstantProduct;
}

template <typename TIn, typename TOut>
TOut
curveSwapIn(
    TAmounts<TIn, TOut> const& pool,
    TIn const& assetIn,
    std::uint16_t tfee,
    std::uint8_t curveType,
    STObject const* ammSle,
    CurveContext const& cctx = {})
{
    if (curveType == CtConstantProduct)
        return swapAssetIn(pool, assetIn, tfee);

    if (auto const* curve = getCurve(curveType, *getCurrentTransactionRules()))
    {
        auto const stPoolIn = toSTAmount(pool.in);
        auto const stPoolOut = toSTAmount(pool.out);
        auto const stAssetIn = toSTAmount(assetIn);
        if (auto const result = curve->swapIn(stPoolIn, stPoolOut, stAssetIn, tfee, ammSle, cctx))
            return get<TOut>(*result);
    }
    return toAmount<TOut>(getAsset(pool.out), 0);
}

template <typename TIn, typename TOut>
TIn
curveSwapOut(
    TAmounts<TIn, TOut> const& pool,
    TOut const& assetOut,
    std::uint16_t tfee,
    std::uint8_t curveType,
    STObject const* ammSle,
    CurveContext const& cctx = {})
{
    if (curveType == CtConstantProduct)
        return swapAssetOut(pool, assetOut, tfee);

    if (auto const* curve = getCurve(curveType, *getCurrentTransactionRules()))
    {
        auto const stPoolIn = toSTAmount(pool.in);
        auto const stPoolOut = toSTAmount(pool.out);
        auto const stAssetOut = toSTAmount(assetOut);
        if (auto const result = curve->swapOut(stPoolIn, stPoolOut, stAssetOut, tfee, ammSle, cctx))
            return get<TIn>(*result);
    }
    return toMaxAmount<TIn>(getAsset(pool.in));
}

}  // namespace xrpl
