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

#ifndef RIPPLE_APP_MISC_AMM_FORMULAE_H_INCLUDED
#define RIPPLE_APP_MISC_AMM_FORMULAE_H_INCLUDED

#include <ripple/basics/IOUAmount.h>
#include <ripple/protocol/Issue.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STAmount.h>

#include <type_traits>

namespace ripple {

namespace detail {

double
saToDouble(STAmount const& a);

template <typename T>
inline std::enable_if_t<(std::is_floating_point<T>::value), std::size_t>
decimal_places(T v)
{
    std::size_t count = 0;
    v = std::abs(v);
    auto c = v - std::floor(v);
    T factor = 10;
    T eps = std::numeric_limits<T>::epsilon() * c;

    while ((c > eps && c < (1 - eps)) &&
           count < std::numeric_limits<T>::max_digits10)
    {
        c = v * factor;
        c = c - std::floor(c);
        factor *= 10;
        eps = std::numeric_limits<T>::epsilon() * v * factor;
        count++;
    }

    return count;
}

STAmount
toSTAfromDouble(double v, Issue const& issue = noIssue());

template <typename T>
T
get(STAmount const& a)
{
    if constexpr (std::is_same_v<T, XRPAmount>)
        return a.xrp();
    else if constexpr (std::is_same_v<T, IOUAmount>)
        return a.iou();
    else
        return a;
}

}  // namespace detail

/** Calculate LP Tokens given AMM pool reserves.
 * @param asset1 AMM one side of the pool reserve
 * @param asset2 AMM another side of the pool reserve
 * @param weight1 xrp pool weight
 * @return LP Tokens as IOU
 */
STAmount
calcAMMLPT(
    STAmount const& asset1,
    STAmount const& asset2,
    Issue const& lptIssue,
    std::uint8_t weight1);

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_AMM_FORMULAE_H_INCLUDED
