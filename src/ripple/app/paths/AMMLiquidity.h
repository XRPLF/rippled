//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_TX_AMMLIQUIDITY_H_INCLUDED
#define RIPPLE_APP_TX_AMMLIQUIDITY_H_INCLUDED

#include <ripple/app/misc/AMMHelpers.h>
#include <ripple/app/misc/AMMUtils.h>
#include <ripple/app/paths/AMMContext.h>
#include <ripple/basics/Log.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/STLedgerEntry.h>

namespace ripple {

template <typename TIn, typename TOut>
class AMMOffer;

/** AMMLiquidity class provides AMM offers to BookStep class.
 * The offers are generated in two ways. If there are multiple
 * paths specified to the payment transaction then the offers
 * are generated based on the Fibonacci sequence with
 * a limited number of payment engine iterations consuming AMM offers.
 * These offers behave the same way as CLOB offers in that if
 * there is a limiting step, then the offers are adjusted
 * based on their quality.
 * If there is only one path specified in the payment transaction
 * then the offers are generated based on the competing CLOB offer
 * quality. In this case the offer's size is set in such a way
 * that the new AMM's pool spot price quality is equal to the CLOB's
 * offer quality.
 */
template <typename TIn, typename TOut>
class AMMLiquidity
{
private:
    inline static const Number InitialFibSeqPct = Number(5) / 20000;
    AMMContext& ammContext_;
    AccountID const ammAccountID_;
    std::uint32_t const tradingFee_;
    Issue const issueIn_;
    Issue const issueOut_;
    // Initial AMM pool balances
    TAmounts<TIn, TOut> const initialBalances_;
    beast::Journal const j_;

public:
    AMMLiquidity(
        ReadView const& view,
        AccountID const& ammAccountID,
        std::uint32_t tradingFee,
        Issue const& in,
        Issue const& out,
        AMMContext& ammContext,
        beast::Journal j);
    ~AMMLiquidity() = default;
    AMMLiquidity(AMMLiquidity const&) = delete;
    AMMLiquidity&
    operator=(AMMLiquidity const&) = delete;

    /** Generate AMM offer. Returns nullopt if clobQuality is provided
     * and it is better than AMM offer quality. Otherwise returns AMM offer.
     * If clobQuality is provided then AMM offer size is set based on the
     * quality.
     */
    std::optional<AMMOffer<TIn, TOut>>
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

    AMMContext&
    context() const
    {
        return ammContext_;
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

private:
    /** Fetches current AMM balances.
     */
    TAmounts<TIn, TOut>
    fetchBalances(ReadView const& view) const;

    /** Generate AMM offers with the offer size based on Fibonacci sequence.
     * The sequence corresponds to the payment engine iterations with AMM
     * liquidity. Iterations that don't consume AMM offers don't count.
     * The number of iterations with AMM offers is limited.
     * If the generated offer exceeds the pool balance then the function
     * throws overflow exception.
     */
    TAmounts<TIn, TOut>
    generateFibSeqOffer(TAmounts<TIn, TOut> const& balances) const;

    /** Generate max offer.
     * If `fixAMMOverflowOffer` is active, the offer is generated as:
     * takerGets = 99% * balances.out takerPays = swapOut(takerGets).
     * Return nullopt if takerGets is 0 or takerGets == balances.out.
     *
     * If `fixAMMOverflowOffer` is not active, the offer is generated as:
     * takerPays = max input amount;
     * takerGets = swapIn(takerPays).
     */
    std::optional<AMMOffer<TIn, TOut>>
    maxOffer(TAmounts<TIn, TOut> const& balances, Rules const& rules) const;
};

}  // namespace ripple

#endif  // RIPPLE_APP_TX_AMMLIQUIDITY_H_INCLUDED
