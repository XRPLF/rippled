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

#ifndef RIPPLE_TX_AMMCONCENTRATEDDEPOSIT_H_INCLUDED
#define RIPPLE_TX_AMMCONCENTRATEDDEPOSIT_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

/** AMMConcentratedDeposit implements adding liquidity to concentrated liquidity
 * positions. This transaction allows liquidity providers to add liquidity to
 * existing concentrated liquidity positions within specific price ranges. The
 * transaction calculates the optimal amounts of both tokens to deposit based on
 * the current price and the specified liquidity amount.
 *
 *  Key features:
 *  - Add liquidity to existing positions
 *  - Automatic amount calculation based on current price
 *  - Slippage protection with maximum amounts
 *  - Fee accumulation tracking
 *  - Position state updates
 *
 *  The transaction:
 *  - Validates the position exists and is owned by the caller
 *  - Calculates optimal token amounts for the liquidity
 *  - Updates position liquidity and fee tracking
 *  - Transfers tokens from the caller to the AMM
 *  - Updates tick data for price tracking
 */
class AMMConcentratedDeposit : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit AMMConcentratedDeposit(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    static TER
    preclaim(PreclaimContext const& ctx);

    /** Attempt to add liquidity to the concentrated liquidity position. */
    TER
    doApply() override;

private:
    /** Validate concentrated liquidity deposit parameters */
    static NotTEC
    validateConcentratedLiquidityDepositParams(
        STTx const& tx,
        beast::Journal const& j);

    /** Calculate optimal amounts for liquidity addition */
    static std::pair<STAmount, STAmount>
    calculateOptimalAmounts(
        STAmount const& liquidity,
        std::uint64_t sqrtPriceX64,
        std::uint64_t sqrtPriceAX64,
        std::uint64_t sqrtPriceBX64);

    /** Update concentrated liquidity position */
    static TER
    updateConcentratedLiquidityPosition(
        ApplyView& view,
        AccountID const& owner,
        std::int32_t tickLower,
        std::int32_t tickUpper,
        std::uint32_t nonce,
        STAmount const& liquidityDelta,
        STAmount const& feeGrowthInside0X128,
        STAmount const& feeGrowthInside1X128,
        beast::Journal const& j);

    /** Update tick data for liquidity changes */
    static TER
    updateTick(
        ApplyView& view,
        std::int32_t tick,
        STAmount const& liquidityNet,
        beast::Journal const& j);
};

}  // namespace ripple

#endif  // RIPPLE_TX_AMMCONCENTRATEDDEPOSIT_H_INCLUDED
