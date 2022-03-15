//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#ifndef RIPPLE_TX_AMMSWAP_H_INCLUDED
#define RIPPLE_TX_AMMSWAP_H_INCLUDED

#include <ripple/app/tx/impl/Transactor.h>

namespace ripple {

class Sandbox;

class AMMSwap : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Custom};

    explicit AMMSwap(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    /** Enforce constraints beyond those of the Transactor base class. */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Enforce constraints beyond those of the Transactor base class. */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Gather information beyond what the Transactor base class gathers. */
    void
    preCompute() override;

    /** Attempt to create the AMM instance. */
    TER
    doApply() override;

private:
    std::pair<TER, bool>
    applyGuts(Sandbox& view);

    /** Swap the assets.
     * @param view
     * @param ammAccount AMM account
     * @param assetIn asset in
     * @param assetOut asset out
     * @param lpAsset current AMM asset balance same issue as assetOut
     * @return
     */
    TER
    swapAssets(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& assetIn,
        STAmount const& assetOut,
        STAmount const& lpAsset);

    /** Swap asset in.
     * @param view
     * @param ammAccount AMM account
     * @param asset1Balance current AMM asset1 balance
     * @param asset2Balance current AMM asset2 balance
     * @param lpAsset current LP asset balance same issue as asset2Balance
     * @param assetIn asset to swap in same issue as asset1Balance
     * @param weight1 asset1 weight
     * @param tfee trading fee in basis points
     * @return
     */
    TER
    swapIn(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& asset2Balance,
        STAmount const& lpAsset,
        STAmount const& assetIn,
        std::uint8_t weight1,
        std::uint16_t tfee);

    /** Swap asset out.
     * @param view
     * @param ammAccount AMM account
     * @param asset1Balance current AMM asset1 balance
     * @param asset2Balance current AMM asset2 balance
     * @param lpAsset current LP asset balance same issue as asset1Balance
     * @param assetOut asset to swap out same issue as asset1Balance
     * @param weight1 asset1 weight
     * @param tfee trading fee in basis points
     * @return
     */
    TER
    swapOut(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& asset2Balance,
        STAmount const& lpAsset,
        STAmount const& assetOut,
        std::uint8_t weight1,
        std::uint16_t tfee);

    /** Swap in asset's issue specified in assetIn. The spot price after
     * the swap does not exceed MaxSP.
     * @param view
     * @param ammAccount AMM account
     * @param asset1Balance current AMM asset1 balance
     * @param asset2Balance current AMM asset2 balance
     * @param lpAsset current LP asset balance same issue as asset2Balance
     * @param assetIn asset's in issue same issue as asset1Balance
     * @param maxSP SP after the trade
     * @param weight1 asset1 weight
     * @param tfee trading fee in basis point
     * @return
     */
    TER
    swapInMaxSP(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& asset2Balance,
        STAmount const& lpAsset,
        Issue const& assetInIssue,
        STAmount const& maxSP,
        std::uint8_t weight1,
        std::uint16_t tfee);

    /**
     * @param view
     * @param ammAccount AMM account
     * @param asset1Balance current AMM asset1 balance
     * @param asset2Balance current AMM asset2 balance
     * @param lpAsset current LP asset same issue as asset2Balance
     * @param assetIn asset to swap in same issue as asset1Balance
     * @param slippage trade slippage in basis points
     * @param weight1 asset1 weight
     * @param tfee trading fee in basis points
     * @return
     */
    TER
    swapInSlippage(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& asset2Balance,
        STAmount const& lpAsset,
        STAmount const& assetIn,
        std::uint16_t slippage,
        std::uint8_t weight1,
        std::uint16_t tfee);

    /**
     * @param view
     * @param ammAccount AMM account
     * @param asset1Balance current AMM asset1 balance
     * @param asset2Balance current AMM asset2 balance
     * @param lpAsset1 current LP asset1
     * @param lpAsset2 current LP asset2
     * @param asset asset to swap in same issue as asset1Balance
     * @param slippage trade slippage in basis points
     * @param maxSP SP after the trade
     * @param weight1 asset1 weight
     * @param tfee trading fee in basis points
     * @return
     */
    TER
    swapSlippageMaxSP(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& asset2Balance,
        STAmount const& lpAsset1,
        STAmount const& lpAsset2,
        STAmount const& asset,
        std::uint16_t slippage,
        STAmount const& maxSP,
        std::uint8_t weight1,
        std::uint16_t tfee);

    /** Swap out asset's issue specified in assetOut. The spot price after
     * the swap does not exceed MaxSP.
     * @param view
     * @param ammAccount AMM account
     * @param asset1Balance current AMM asset1 balance
     * @param asset2Balance current AMM asset2 balance
     * @param lpAsset current LP asset2 balance same issue as asset1Balance
     * @param assetOut asset's in issue same issue as asset1Balance
     * @param maxSP SP after the trade
     * @param weight1 asset1 weight
     * @param tfee trading fee in basis point
     * @return
     */
    TER
    swapOutMaxSP(
        Sandbox& view,
        AccountID const& ammAccount,
        STAmount const& asset1Balance,
        STAmount const& asset2Balance,
        STAmount const& lpAsset,
        Issue const& assetOutIssue,
        STAmount const& maxSP,
        std::uint8_t weight1,
        std::uint16_t tfee);
};

}  // namespace ripple

#endif  // RIPPLE_TX_AMMSWAP_H_INCLUDED
