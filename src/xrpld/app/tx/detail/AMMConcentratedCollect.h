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

#ifndef RIPPLE_TX_AMMCONCENTRATEDCOLLECT_H_INCLUDED
#define RIPPLE_TX_AMMCONCENTRATEDCOLLECT_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

/** AMMConcentratedCollect implements collecting accumulated fees from
 * concentrated liquidity positions. This transaction allows liquidity providers
 * to collect fees that have accumulated in their positions without removing
 * the underlying liquidity.
 *
 *  Key features:
 *  - Collect accumulated fees from positions
 *  - Maintain position liquidity unchanged
 *  - Update fee tracking state
 *  - Transfer collected fees to position owner
 *
 *  The transaction:
 *  - Validates the position exists and is owned by the caller
 *  - Calculates accumulated fees since last collection
 *  - Updates position fee tracking state
 *  - Transfers collected fees to the position owner
 */
class AMMConcentratedCollect : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit AMMConcentratedCollect(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    static TER
    preclaim(PreclaimContext const& ctx);

    /** Attempt to collect fees from the concentrated liquidity position. */
    TER
    doApply() override;

private:
    /** Validate concentrated liquidity collect parameters */
    static NotTEC
    validateConcentratedLiquidityCollectParams(
        STTx const& tx,
        beast::Journal const& j);

    /** Calculate accumulated fees for the position */
    static std::pair<STAmount, STAmount>
    calculateAccumulatedFees(
        ReadView const& view,
        AccountID const& owner,
        std::int32_t tickLower,
        std::int32_t tickUpper,
        std::uint32_t nonce,
        beast::Journal const& j);

    /** Update position fee tracking after collection */
    static TER
    updatePositionFeeTracking(
        ApplyView& view,
        AccountID const& owner,
        std::int32_t tickLower,
        std::int32_t tickUpper,
        std::uint32_t nonce,
        STAmount const& feeGrowthInside0X128,
        STAmount const& feeGrowthInside1X128,
        beast::Journal const& j);
};

}  // namespace ripple

#endif  // RIPPLE_TX_AMMCONCENTRATEDCOLLECT_H_INCLUDED
