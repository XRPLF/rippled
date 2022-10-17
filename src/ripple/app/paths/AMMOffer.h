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

#ifndef RIPPLE_APP_AMMOFFER_H_INCLUDED
#define RIPPLE_APP_AMMOFFER_H_INCLUDED

#include <ripple/ledger/ApplyView.h>
#include <ripple/protocol/Quality.h>

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
    // For one-path it is either the pool balances or the size such that
    // if the offer is consumed then its pool SP quality is equal to
    // competing CLOB offer quality.
    TAmounts<TIn, TOut> const amounts_;
    // If seated then current pool balances. Used in one-path limiting steps
    // to swap in/out.
    std::optional<TAmounts<TIn, TOut>> const balances_;

public:
    AMMOffer(
        AMMLiquidity<TIn, TOut> const& ammLiquidity,
        TAmounts<TIn, TOut> const& offer,
        std::optional<TAmounts<TIn, TOut>> const& balances);

    Quality const
    quality() const noexcept
    {
        return Quality{amounts_};
    }

    Issue
    issueIn() const;

    Issue
    issueOut() const;

    AccountID const&
    owner() const;

    uint256
    key() const
    {
        return uint256{beast::zero};
    }

    TAmounts<TIn, TOut> const&
    amount() const;

    void
    consume(ApplyView& view, TAmounts<TIn, TOut> const& consumed);

    bool
    fully_consumed() const
    {
        return true;
    }

    /** Limit out of the provided offer. If one-path then swapOut
     * using current balances. If multi-path then ceil_out using
     * current quality.
     */
    TAmounts<TIn, TOut>
    limitOut(TAmounts<TIn, TOut> const& offrAmt, TOut const& limit) const;

    /** Limit in of the provided offer. If one-path then swapIn
     * using current balances. If multi-path then ceil_in using
     * current quality.
     */
    TAmounts<TIn, TOut>
    limitIn(TAmounts<TIn, TOut> const& offrAmt, TIn const& limit) const;

    QualityFunction
    getQF() const;
};

}  // namespace ripple

#endif  // RIPPLE_APP_AMMOFFER_H_INCLUDED
