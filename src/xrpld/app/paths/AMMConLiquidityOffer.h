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

#ifndef RIPPLE_APP_AMMCONLIQUIDITYOFFER_H_INCLUDED
#define RIPPLE_APP_AMMCONLIQUIDITYOFFER_H_INCLUDED

#include <xrpld/ledger/ApplyView.h>
#include <xrpld/ledger/View.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

namespace ripple {

template <typename TIn, typename TOut>
using TAmountPair = std::pair<TIn, TOut>;

template <typename TIn, typename TOut>
class AMMConLiquidityPool;
class QualityFunction;

/** Represents synthetic concentrated liquidity offer in AMMConLiquidityStep.
 * AMMConLiquidityOffer mirrors TOffer methods for use in generic step methods.
 * AMMConLiquidityOffer amounts are changed indirectly in limiting steps.
 */
template <typename TIn, typename TOut>
class AMMConLiquidityOffer
{
private:
    AMMConLiquidityPool<TIn, TOut> const& ammConLiquidity_;
    // Initial offer amounts based on aggregated concentrated liquidity
    // positions
    TAmounts<TIn, TOut> const amounts_;
    // Current aggregated liquidity from positions within the price range
    TAmounts<TIn, TOut> const balances_;
    // The quality based on current price and liquidity distribution
    Quality const quality_;
    // Concentrated liquidity offer can be consumed once at a given iteration
    bool consumed_;
    // Current sqrt price in Q64.64 format
    std::uint64_t const sqrtPriceX64_;
    // Tick range for this offer
    std::int32_t const tickLower_;
    std::int32_t const tickUpper_;

public:
    AMMConLiquidityOffer(
        AMMConLiquidityPool<TIn, TOut> const& ammConLiquidity,
        TAmountPair<TIn, TOut> const& amounts,
        TAmountPair<TIn, TOut> const& balances,
        Quality const& quality,
        std::uint64_t sqrtPriceX64,
        std::int32_t tickLower,
        std::int32_t tickUpper);

    Quality
    quality() const noexcept
    {
        return quality_;
    }

    Issue const&
    issueIn() const;

    AccountID const&
    owner() const;

    std::optional<uint256>
    key() const
    {
        return std::nullopt;
    }

    TAmountPair<TIn, TOut> const&
    amount() const;

    void
    consume(ApplyView& view, TAmountPair<TIn, TOut> const& consumed);

    bool
    fully_consumed() const
    {
        return consumed_;
    }

    /** Limit out of the provided offer based on concentrated liquidity
     * constraints */
    TAmountPair<TIn, TOut>
    limitOut(TAmountPair<TIn, TOut> const& ofrAmt, TOut const& limit, bool roundUp)
        const;

    /** Limit in of the provided offer based on concentrated liquidity
     * constraints */
    TAmountPair<TIn, TOut>
    limitIn(TAmountPair<TIn, TOut> const& ofrAmt, TIn const& limit, bool roundUp)
        const;

    /** Check if the offer is funded within the concentrated liquidity range */
    bool
    isFunded() const;

    /** Get the owner's funds within the concentrated liquidity range */
    TOut
    ownerFunds() const;

    /** Send assets between accounts within the concentrated liquidity context
     */
    TER
    send(
        ApplyView& view,
        AccountID const& from,
        AccountID const& to,
        STAmount const& amount,
        beast::Journal j) const;

    /** Check invariant for concentrated liquidity positions */
    bool
    checkInvariant(TAmountPair<TIn, TOut> const& amounts, beast::Journal j) const;

    /** Get current sqrt price */
    std::uint64_t
    getSqrtPriceX64() const
    {
        return sqrtPriceX64_;
    }

    /** Get tick range */
    std::pair<std::int32_t, std::int32_t>
    getTickRange() const
    {
        return {tickLower_, tickUpper_};
    }
};

}  // namespace ripple

#endif  // RIPPLE_APP_AMMCONLIQUIDITYOFFER_H_INCLUDED
