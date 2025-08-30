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

#ifndef RIPPLE_TX_AMMCONCENTRATEDWITHDRAW_H_INCLUDED
#define RIPPLE_TX_AMMCONCENTRATEDWITHDRAW_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

/** AMMConcentratedWithdraw implements removing liquidity from concentrated
 * liquidity positions. This transaction allows liquidity providers to remove
 * liquidity from existing concentrated liquidity positions and receive back
 * the underlying tokens plus any accumulated fees.
 *
 *  Key features:
 *  - Remove liquidity from existing positions
 *  - Automatic amount calculation based on current price
 *  - Slippage protection with minimum amounts
 *  - Fee collection during withdrawal
 *  - Position state updates
 *
 *  The transaction:
 *  - Validates the position exists and is owned by the caller
 *  - Calculates optimal token amounts for the liquidity removal
 *  - Updates position liquidity and fee tracking
 *  - Transfers tokens from the AMM to the caller
 *  - Updates tick data for price tracking
 */
class AMMConcentratedWithdraw : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit AMMConcentratedWithdraw(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    static TER
    preclaim(PreclaimContext const& ctx);

    /** Attempt to remove liquidity from the concentrated liquidity position. */
    TER
    doApply() override;

private:
    /** Validate concentrated liquidity withdraw parameters */
    static NotTEC
    validateConcentratedLiquidityWithdrawParams(
        STTx const& tx,
        beast::Journal const& j);

    /** Calculate return amounts for liquidity removal */
    static std::pair<STAmount, STAmount>
    calculateReturnAmounts(
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

    /** Collect accumulated fees from position */
    static TER
    collectFees(
        ApplyView& view,
        AccountID const& owner,
        std::int32_t tickLower,
        std::int32_t tickUpper,
        std::uint32_t nonce,
        beast::Journal const& j);
};

}  // namespace ripple

#endif  // RIPPLE_TX_AMMCONCENTRATEDWITHDRAW_H_INCLUDED
