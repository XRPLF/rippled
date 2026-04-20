// Pluggable AMM curve architecture.
// Concentrated liquidity (CurveType 1) based on XRPL-Standards Discussion #427
// by Roman Thpt (@RomThpt), which adapted Uniswap v3 tick math, fee tier
// structure, and fee accounting to the XRPL. This implementation extends that
// work with a pluggable curve interface, StableSwap, and Smart AMM.
// See: https://github.com/XRPLF/XRPL-Standards/discussions/427

#pragma once

#include <xrpl/basics/Expected.h>
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

struct CurveContext
{
    ReadView const* view = nullptr;
    uint256 const* ammID = nullptr;
};

class CurveInterface
{
public:
    virtual ~CurveInterface() = default;

    virtual Expected<STAmount, TER>
    swapIn(
        STAmount const& poolIn,
        STAmount const& poolOut,
        STAmount const& assetIn,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& ctx = {}) const = 0;

    virtual Expected<STAmount, TER>
    swapOut(
        STAmount const& poolIn,
        STAmount const& poolOut,
        STAmount const& assetOut,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& ctx = {}) const = 0;

    virtual Expected<Number, TER>
    spotPrice(
        STAmount const& poolIn,
        STAmount const& poolOut,
        std::uint16_t tfee,
        STObject const* ammSle,
        CurveContext const& ctx = {}) const = 0;

    [[nodiscard]] virtual TER
    validateParams(STObject const& tx) const = 0;

    virtual Expected<STAmount, TER>
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
};

CurveInterface const*
getCurve(std::uint8_t curveType, Rules const& rules);

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
