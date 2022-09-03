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

#include <ripple/basics/Number.h>

namespace ripple {

class Quality;

/** Average Quality as function of out: q(out) = m * out + b,
 * where m = -1 / poolGets, b = poolPays / poolGets. Used
 * to find required output amount when quality limit is
 * provided for one path optimization. This implementation
 * may possibly lead to using this approach for multiple paths.
 * Current issue with is AMM and CLOB offer dependency between
 * the paths. AMM is more complicated of two. But CLOB offer
 * dependency also requires some tweaking. If a CLOB offer
 * is used in calculating quality function in one path, then
 * it can't be used in calculating quality function in another
 * path. Moreover, we need to find a path, which would have
 * the best quality with this CLOB offer.
 */
class QualityFunction
{
private:
    Number m_;  // slope
    Number b_;  // intercept

public:
    QualityFunction(Quality const& quality);
    QualityFunction(Amounts const& amounts);
    QualityFunction();
    ~QualityFunction() = default;

    /** Combines QF with the next step QF
     */
    void
    combineWithNext(QualityFunction const& qf);

    /** Find output to produce the requested
     * instant quality (Spot Price Quality).
     * @param quality requested instant quality (quality limit)
     */
    std::optional<Number>
    outFromInstQ(Quality const& quality);

    /** Return true if the quality function is constant
     */
    bool
    isConst() const
    {
        return m_ == 0;
    }
};

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_QUALITYFUNCTION_H_INCLUDED
