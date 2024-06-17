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

#ifndef RIPPLE_APP_AMMOFFER_H_INCLUDED
#define RIPPLE_APP_AMMOFFER_H_INCLUDED

#include <ripple/ledger/ApplyView.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/TER.h>

namespace ripple {

template <typename TIn, typename TOut>
class AMMLiquidity;
class QualityFunction;

/** Represents synthetic AMM offer in BookStep. AMMOffer mirrors TOffer
 * methods for use in generic BookStep methods. AMMOffer amounts
 * are changed indirectly in BookStep limiting steps.
 */
template <typename TIn, typename TOut>
class AMMOffer
{
private:
    AMMLiquidity<TIn, TOut> const& ammLiquidity_;
    // Initial offer amounts. It is fibonacci seq generated for multi-path.
    // If the offer size is set based on the competing CLOB offer then
    // the AMM offer size is such that if the offer is consumed then
    // the updated AMM pool SP quality is going to be equal to competing
    // CLOB offer quality. If there is no competing CLOB offer then
    // the initial size is set to in=cMax[Native,Value],balances.out.
    // While this is not a "real" offer it simulates the case of
    // the swap out of the entire side of the pool, in which case
    // the swap in amount is infinite.
    TAmounts<TIn, TOut> const amounts_;
    // Current pool balances.
    TAmounts<TIn, TOut> const balances_;
    // The Spot Price quality if balances != amounts
    // else the amounts quality
    Quality const quality_;
    // AMM offer can be consumed once at a given iteration
    bool consumed_;

public:
    AMMOffer(
        AMMLiquidity<TIn, TOut> const& ammLiquidity,
        TAmounts<TIn, TOut> const& amounts,
        TAmounts<TIn, TOut> const& balances,
        Quality const& quality);

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

    TAmounts<TIn, TOut> const&
    amount() const;

    void
    consume(ApplyView& view, TAmounts<TIn, TOut> const& consumed);

    bool
    fully_consumed() const
    {
        return consumed_;
    }

    /** Limit out of the provided offer. If one-path then swapOut
     * using current balances. If multi-path then ceil_out using
     * current quality.
     */
    TAmounts<TIn, TOut>
    limitOut(
        TAmounts<TIn, TOut> const& offrAmt,
        TOut const& limit,
        bool roundUp) const;

    /** Limit in of the provided offer. If one-path then swapIn
     * using current balances. If multi-path then ceil_in using
     * current quality.
     */
    TAmounts<TIn, TOut>
    limitIn(TAmounts<TIn, TOut> const& offrAmt, TIn const& limit, bool roundUp)
        const;

    QualityFunction
    getQualityFunc() const;

    /** Send funds without incurring the transfer fee
     */
    template <typename... Args>
    static TER
    send(Args&&... args)
    {
        return accountSend(std::forward<Args>(args)..., WaiveTransferFee::Yes);
    }

    bool
    isFunded() const
    {
        // AMM offer is fully funded by the pool
        return true;
    }

    static std::pair<std::uint32_t, std::uint32_t>
    adjustRates(std::uint32_t ofrInRate, std::uint32_t ofrOutRate)
    {
        // AMM doesn't pay transfer fee on Payment tx
        return {ofrInRate, QUALITY_ONE};
    }

    /** Check the new pool product is greater or equal to the old pool
     * product or if decreases then within some threshold.
     */
    bool
    checkInvariant(TAmounts<TIn, TOut> const& consumed, beast::Journal j) const;
};

}  // namespace ripple

#endif  // RIPPLE_APP_AMMOFFER_H_INCLUDED
