//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_AMOUNTCONVERSION_H_INCLUDED
#define RIPPLE_PROTOCOL_AMOUNTCONVERSION_H_INCLUDED

#include <ripple/basics/IOUAmount.h>
#include <ripple/basics/XRPAmount.h>
#include <ripple/protocol/STAmount.h>

namespace ripple {

inline STAmount
toSTAmount(IOUAmount const& iou, Issue const& iss)
{
    bool const isNeg = iou.signum() < 0;
    std::uint64_t const umant = isNeg ? -iou.mantissa() : iou.mantissa();
    return STAmount(
        iss,
        umant,
        iou.exponent(),
        /*native*/ false,
        isNeg,
        STAmount::unchecked());
}

inline STAmount
toSTAmount(IOUAmount const& iou)
{
    return toSTAmount(iou, noIssue());
}

inline STAmount
toSTAmount(XRPAmount const& xrp)
{
    bool const isNeg = xrp.signum() < 0;
    std::uint64_t const umant = isNeg ? -xrp.drops() : xrp.drops();
    return STAmount(umant, isNeg);
}

inline STAmount
toSTAmount(XRPAmount const& xrp, Issue const& iss)
{
    assert(isXRP(iss.account) && isXRP(iss.currency));
    return toSTAmount(xrp);
}

template <class T>
T
toAmount(STAmount const& amt) = delete;

template <>
inline STAmount
toAmount<STAmount>(STAmount const& amt)
{
    return amt;
}

template <>
inline IOUAmount
toAmount<IOUAmount>(STAmount const& amt)
{
    assert(amt.mantissa() < std::numeric_limits<std::int64_t>::max());
    bool const isNeg = amt.negative();
    std::int64_t const sMant =
        isNeg ? -std::int64_t(amt.mantissa()) : amt.mantissa();

    assert(!isXRP(amt));
    return IOUAmount(sMant, amt.exponent());
}

template <>
inline XRPAmount
toAmount<XRPAmount>(STAmount const& amt)
{
    assert(amt.mantissa() < std::numeric_limits<std::int64_t>::max());
    bool const isNeg = amt.negative();
    std::int64_t const sMant =
        isNeg ? -std::int64_t(amt.mantissa()) : amt.mantissa();

    assert(isXRP(amt));
    return XRPAmount(sMant);
}

template <class T>
T
toAmount(IOUAmount const& amt) = delete;

template <>
inline IOUAmount
toAmount<IOUAmount>(IOUAmount const& amt)
{
    return amt;
}

template <class T>
T
toAmount(XRPAmount const& amt) = delete;

template <>
inline XRPAmount
toAmount<XRPAmount>(XRPAmount const& amt)
{
    return amt;
}

template <typename T>
T
toAmount(
    Issue const& issue,
    Number const& n,
    Number::rounding_mode mode = Number::getround())
{
    saveNumberRoundMode rm(Number::getround());
    if (isXRP(issue))
        Number::setround(mode);
    if constexpr (std::is_same_v<IOUAmount, T>)
        return IOUAmount(n);
    if constexpr (std::is_same_v<XRPAmount, T>)
        return XRPAmount(static_cast<std::int64_t>(n));
    if constexpr (std::is_same_v<STAmount, T>)
    {
        if (isXRP(issue))
            return STAmount(issue, static_cast<std::int64_t>(n));
        return STAmount(issue, n.mantissa(), n.exponent());
    }
}

inline STAmount
toSTAmount(
    Issue const& issue,
    Number const& n,
    Number::rounding_mode mode = Number::getround())
{
    return toAmount<STAmount>(issue, n, mode);
}

template <typename T>
Issue
getIssue(T const& amt)
{
    if constexpr (std::is_same_v<IOUAmount, T>)
        return noIssue();
    if constexpr (std::is_same_v<XRPAmount, T>)
        return xrpIssue();
    if constexpr (std::is_same_v<STAmount, T>)
        return amt.issue();
}

template <typename T>
constexpr T
get(STAmount const& a)
{
    if constexpr (std::is_same_v<IOUAmount, T>)
        return a.iou();
    if constexpr (std::is_same_v<XRPAmount, T>)
        return a.xrp();
    if constexpr (std::is_same_v<STAmount, T>)
        return a;
}

}  // namespace ripple

#endif
