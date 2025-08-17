//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_APP_AMMCONLIQUIDITYPOOL_H_INCLUDED
#define RIPPLE_APP_AMMCONLIQUIDITYPOOL_H_INCLUDED

#include <xrpld/app/misc/AMMHelpers.h>
#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/app/paths/AMMContext.h>
#include <xrpld/ledger/ReadView.h>
#include <xrpld/ledger/View.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/STAmount.h>

#include <vector>

namespace ripple {

template <typename TIn, typename TOut>
class AMMConLiquidityOffer;

/** AMMConLiquidityPool class provides concentrated liquidity offers to
 * AMMConLiquidityStep. This class aggregates liquidity from multiple
 * concentrated liquidity positions within a price range and generates offers
 * based on the current price and available liquidity.
 */
template <typename TIn, typename TOut>
class AMMConLiquidityPool
{
private:
    inline static Number const InitialFibSeqPct = Number(5) / 20000;
    AMMContext& ammContext_;
    AccountID const ammAccountID_;
    std::uint32_t const tradingFee_;
    Issue const issueIn_;
    Issue const issueOut_;
    // Current sqrt price in Q64.64 format
    std::uint64_t sqrtPriceX64_;
    // Current tick
    std::int32_t currentTick_;
    // Aggregated liquidity from positions within the active range
    STAmount aggregatedLiquidity_;
    beast::Journal const j_;

public:
    AMMConLiquidityPool(
        ReadView const& view,
        AccountID const& ammAccountID,
        std::uint32_t tradingFee,
        Issue const& in,
        Issue const& out,
        AMMContext& ammContext,
        beast::Journal j);
    ~AMMConLiquidityPool() = default;
    AMMConLiquidityPool(AMMConLiquidityPool const&) = delete;
    AMMConLiquidityPool&
    operator=(AMMConLiquidityPool const&) = delete;

    /** Generate concentrated liquidity offer. Returns nullopt if clobQuality is
     * provided and it is better than concentrated liquidity offer quality.
     * Otherwise returns offer. If clobQuality is provided then offer size is
     * set based on the quality.
     */
    std::optional<AMMConLiquidityOffer<TIn, TOut>>
    getOffer(ReadView const& view, std::optional<Quality> const& clobQuality)
        const;

    AccountID const&
    ammAccount() const
    {
        return ammAccountID_;
    }

    bool
    multiPath() const
    {
        return ammContext_.multiPath();
    }

    std::uint32_t
    tradingFee() const
    {
        return tradingFee_;
    }

    Issue const&
    issueIn() const
    {
        return issueIn_;
    }

    Issue const&
    issueOut() const
    {
        return issueOut_;
    }

    std::uint64_t
    getSqrtPriceX64() const
    {
        return sqrtPriceX64_;
    }

    std::int32_t
    getCurrentTick() const
    {
        return currentTick_;
    }

    STAmount const&
    getAggregatedLiquidity() const
    {
        return aggregatedLiquidity_;
    }

private:
    /** Calculate the amount of liquidity available within a price range */
    STAmount
    calculateAvailableLiquidity(
        ReadView const& view,
        std::int32_t tickLower,
        std::int32_t tickUpper) const;

    /** Calculate the amounts for a given liquidity and price range */
    std::pair<STAmount, STAmount>
    calculateAmountsForLiquidity(
        STAmount const& liquidity,
        std::uint64_t sqrtPriceX64,
        std::uint64_t sqrtPriceAX64,
        std::uint64_t sqrtPriceBX64) const;

    /** Find all active concentrated liquidity positions for this AMM */
    std::map<AccountID, STAmount>
    findActivePositions(ReadView const& view) const;

    /** Calculate the quality for a given price and liquidity */
    Quality
    calculateQuality(std::uint64_t sqrtPriceX64, STAmount const& liquidity)
        const;

    /** Find all concentrated liquidity positions within the active price range
     */
    std::vector<std::pair<AccountID, STAmount>>
    findActivePositions(ReadView const& view) const;

    /** Update fee growth for positions after a trade */
    void
    updateFeeGrowth(ApplyView& view, STAmount const& fee0, STAmount const& fee1)
        const;
};

}  // namespace ripple

#endif  // RIPPLE_APP_AMMCONLIQUIDITYPOOL_H_INCLUDED
