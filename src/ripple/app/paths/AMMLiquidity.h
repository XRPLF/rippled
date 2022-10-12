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

#ifndef RIPPLE_APP_TX_AMMOFFERMAKER_H_INCLUDED
#define RIPPLE_APP_TX_AMMOFFERMAKER_H_INCLUDED

#include "ripple/app/misc/AMM.h"
#include "ripple/app/misc/AMM_formulae.h"
#include "ripple/app/paths/AMMOfferCounter.h"
#include "ripple/basics/Log.h"
#include "ripple/ledger/ReadView.h"
#include "ripple/ledger/View.h"
#include "ripple/protocol/Quality.h"
#include "ripple/protocol/STLedgerEntry.h"

namespace ripple {

namespace detail {

/** Generate AMM offers with the offer size based on Fibonacci sequence.
 * The sequence corresponds to the payment engine iterations with AMM
 * liquidity. Iterations that don't consume AMM offers don't count.
 * We max out at four iterations with AMM offers.
 */
class FibSeqHelper
{
private:
    // Current sequence amounts.
    Amounts curSeq_{};
    // Latest sequence number.
    std::uint16_t lastNSeq_{0};
    Number x_{0};
    Number y_{0};

public:
    FibSeqHelper() = default;
    ~FibSeqHelper() = default;
    FibSeqHelper(FibSeqHelper const&) = delete;
    FibSeqHelper&
    operator=(FibSeqHelper const&) = delete;

    /** Generate first sequence.
     * @param balances current AMM pool balances.
     * @param tfee trading fee in basis points.
     * @return
     */
    Amounts const&
    firstSeq(Amounts const& balances, std::uint16_t tfee);

    /** Generate next sequence.
     * @param n sequence to generate
     * @param balances current AMM pool balances.
     * @param tfee trading fee in basis points.
     * @return
     */
    Amounts const&
    nextNthSeq(std::uint16_t n, Amounts const& balances, std::uint16_t tfee);
};

}  // namespace detail

/** AMMLiquidity class provides AMM offers to BookStep class.
 * The offers are generated in two ways. If there are multiple
 * paths specified to the payment transaction then the offers
 * are generated based on the Fibonacci sequence with
 * at most four payment engine iterations consuming AMM offers.
 * These offers behave the same way as CLOB offers in that if
 * there is a limiting step, then the offers are adjusted
 * based on their quality.
 * If there is only one path specified in the payment transaction
 * then the offers are generated based on the competing CLOB offer
 * quality. In this case, the offer's size is set in such a way
 * that the new AMM's pool spot price quality is equal to the CLOB's
 * offer quality.
 */
class AMMLiquidity
{
private:
    AMMOfferCounter& offerCounter_;
    AccountID const ammAccountID_;
    std::uint32_t const tradingFee_;
    // Cached AMM pool balances as of last getOffer() if not empty().
    // Set to zero if balances have to be re-fetched.
    Amounts balances_;
    // Is seated if multi-path. Generates Fibonacci sequence offer.
    std::optional<detail::FibSeqHelper> fibSeqHelper_;
    beast::Journal const j_;

public:
    AMMLiquidity(
        ReadView const& view,
        AccountID const& ammAccountID,
        std::uint32_t tradingFee,
        Issue const& in,
        Issue const& out,
        AMMOfferCounter& offerCounter,
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
    std::optional<Amounts>
    getOffer(ReadView const& view, std::optional<Quality> const& clobQuality);

    /** Called when AMM offer is consumed. Sets dirty flag
     * to indicate that the balances may have changed and
     * increments offer counter to indicate that AMM offer
     * is used in the strand.
     */
    void
    consumed()
    {
        balances_.in = beast::zero;
        balances_.out = beast::zero;
        offerCounter_.incrementCounter();
    }

    AccountID const&
    ammAccount() const
    {
        return ammAccountID_;
    }

    bool
    multiPath() const
    {
        return offerCounter_.multiPath();
    }

    template <typename TOut>
    STAmount
    swapOut(TOut const& out) const
    {
        return swapAssetOut(balances_, out, tradingFee_);
    }

    template <typename TIn>
    STAmount
    swapIn(TIn const& in) const
    {
        return swapAssetIn(balances_, in, tradingFee_);
    }

    Amounts const&
    balances() const
    {
        return balances_;
    }

private:
    /** Fetches AMM balances if balances_ is empty()
     */
    Amounts
    fetchBalances(ReadView const& view) const;

    /** Returns total amount held by AMM for the given token.
     */
    STAmount
    ammAccountHolds(
        ReadView const& view,
        AccountID const& ammAccountID,
        Issue const& issue) const;

    /** Generate offer based on Fibonacci sequence.
     * @param balances current AMM balances
     */
    Amounts
    generateFibSeqOffer(Amounts const& balances);
};

}  // namespace ripple

#endif  // RIPPLE_APP_TX_AMMOFFERMAKER_H_INCLUDED
