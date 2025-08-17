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

#ifndef RIPPLE_TX_AMMCONCENTRATEDCREATE_H_INCLUDED
#define RIPPLE_TX_AMMCONCENTRATEDCREATE_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

/** AMMConcentratedCreate implements Concentrated Liquidity AMM creation.
 *  This transaction creates a new concentrated liquidity AMM instance with
 *  specified price range and initial liquidity. Unlike traditional AMMs,
 *  concentrated liquidity allows LPs to provide liquidity within specific
 *  price ranges, enabling more efficient capital utilization and better
 *  fee generation.
 *
 *  Key features:
 *  - Price range specification via tick boundaries
 *  - Initial liquidity provision within the range
 *  - Tick spacing for gas optimization
 *  - Position-based liquidity management
 *  - Fee collection within price ranges
 *
 *  The transaction creates:
 *  - AMM account with concentrated liquidity support
 *  - Initial position for the creator
 *  - Tick data structures for price tracking
 *  - Position tracking for fee distribution
 */
class AMMConcentratedCreate : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit AMMConcentratedCreate(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    static TER
    preclaim(PreclaimContext const& ctx);

    /** Attempt to create the concentrated liquidity AMM instance. */
    TER
    doApply() override;

private:
    /** Validate concentrated liquidity parameters */
    static NotTEC
    validateConcentratedLiquidityParams(
        STTx const& tx,
        beast::Journal const& j);

    /** Create concentrated liquidity position */
    static TER
    createConcentratedLiquidityPosition(
        ApplyView& view,
        AccountID const& owner,
        STAmount const& amount0,
        STAmount const& amount1,
        std::int32_t tickLower,
        std::int32_t tickUpper,
        STAmount const& liquidity,
        std::uint32_t nonce,
        beast::Journal const& j);

    /** Initialize tick data */
    static TER
    initializeTick(
        ApplyView& view,
        std::int32_t tick,
        STAmount const& liquidityNet,
        beast::Journal const& j);
};

}  // namespace ripple

#endif  // RIPPLE_TX_AMMCONCENTRATEDCREATE_H_INCLUDED
