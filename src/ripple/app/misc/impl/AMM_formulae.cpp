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

#include <ripple/app/misc/AMM_formulae.h>

#include <cmath>

namespace ripple {

namespace detail {
#pragma message( \
    "THIS IS TEMPORARY. PORTABLE POW() AND LIB FOR IOU/XRP OPS MUST BE IMPLEMENTED.")

double
saToDouble(STAmount const& a)
{
    return static_cast<double>(a.mantissa() * std::pow(10, a.exponent()));
}

STAmount
toSTAfromDouble(double v, Issue const& issue)
{
    auto exponent = decimal_places(v);
    std::int64_t mantissa = v * pow(10, exponent);
    exponent = -exponent;
    return STAmount(issue, mantissa, exponent);
}

STAmount
sqrt(STAmount const& a)
{
    return toSTAfromDouble(std::sqrt(saToDouble(a)), a.issue());
}

}  // namespace detail

STAmount
calcAMMLPT(
    STAmount const& asset1,
    STAmount const& asset2,
    Issue const& lptIssue,
    std::uint8_t weight1)
{
    assert(weight1 == 50);

    return detail::sqrt(multiply(asset1, asset2, lptIssue));
}

}  // namespace ripple
