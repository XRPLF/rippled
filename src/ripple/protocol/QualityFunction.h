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

#ifndef RIPPLE_PROTOCOL_QUALITYFUNCTION_H_INCLUDED
#define RIPPLE_PROTOCOL_QUALITYFUNCTION_H_INCLUDED

#include <ripple/app/misc/AMM_formulae.h>
#include <ripple/basics/Number.h>

namespace ripple {

class Quality;

/** Average Quality as a function of out: q(out) = m * out + b,
 * where m = -1 / poolGets, b = poolPays / poolGets. Used
 * to find required output amount when quality limit is
 * provided for one path optimization.
 */
class QualityFunction
{
private:
    // slope
    Number m_;
    // intercept
    Number b_;
    // seated if QF is for CLOB offer.
    // note that m_ is 0 in this case
    std::optional<Quality> quality_;

public:
    struct AMMTag
    {
    };
    // AMMOffer for multi-path is like CLOB, i.e. the offer size
    // changes proportionally to its quality.
    struct CLOBLikeTag
    {
    };
    QualityFunction(Quality const& quality, CLOBLikeTag);
    template <typename TIn, typename TOut>
    QualityFunction(
        TAmounts<TIn, TOut> const& amounts,
        std::uint32_t tfee,
        AMMTag);
    QualityFunction();

    /** Combines QF with the next step QF
     */
    void
    combineWithNext(QualityFunction const& qf);

    /** Find output to produce the requested
     * average quality.
     * @param quality requested average quality (quality limit)
     */
    std::optional<Number>
    outFromAvgQ(Quality const& quality);

    /** Return true if the quality function is constant
     */
    bool
    isConst() const
    {
        return quality_.has_value();
    }

    std::optional<Quality>
    quality() const
    {
        return quality_;
    }
};

template <typename TIn, typename TOut>
QualityFunction::QualityFunction(
    TAmounts<TIn, TOut> const& amounts,
    std::uint32_t tfee,
    QualityFunction::AMMTag)
{
    if (amounts.in <= beast::zero || amounts.out <= beast::zero)
        Throw<std::runtime_error>("QualityFunction amounts are 0.");
    auto const cfee = feeMult(tfee);
    m_ = -cfee / amounts.in;
    b_ = amounts.out * cfee / amounts.in;
}

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_QUALITYFUNCTION_H_INCLUDED
