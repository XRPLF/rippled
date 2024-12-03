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

#ifndef RIPPLE_PATH_IMPL_AMOUNTSPEC_H_INCLUDED
#define RIPPLE_PATH_IMPL_AMOUNTSPEC_H_INCLUDED

#include <xrpl/basics/IOUAmount.h>
#include <xrpl/basics/XRPAmount.h>
#include <xrpl/protocol/STAmount.h>

#include <optional>

namespace ripple {

struct AmountSpec
{
    explicit AmountSpec() = default;

    bool native;
    union
    {
        XRPAmount xrp;
        IOUAmount iou = {};
    };
    std::optional<AccountID> issuer;
    std::optional<Currency> currency;

    friend std::ostream&
    operator<<(std::ostream& stream, AmountSpec const& amt)
    {
        if (amt.native)
            stream << to_string(amt.xrp);
        else
            stream << to_string(amt.iou);
        if (amt.currency)
            stream << "/(" << *amt.currency << ")";
        if (amt.issuer)
            stream << "/" << *amt.issuer << "";
        return stream;
    }
};

struct EitherAmount
{
#ifndef NDEBUG
    bool native = false;
#endif

    union
    {
        IOUAmount iou = {};
        XRPAmount xrp;
    };

    EitherAmount() = default;

    explicit EitherAmount(IOUAmount const& a) : iou(a)
    {
    }

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
    // ignore warning about half of iou amount being uninitialized
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    explicit EitherAmount(XRPAmount const& a) : xrp(a)
    {
#ifndef NDEBUG
        native = true;
#endif
    }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

    explicit EitherAmount(AmountSpec const& a)
    {
#ifndef NDEBUG
        native = a.native;
#endif
        if (a.native)
            xrp = a.xrp;
        else
            iou = a.iou;
    }

#ifndef NDEBUG
    friend std::ostream&
    operator<<(std::ostream& stream, EitherAmount const& amt)
    {
        if (amt.native)
            stream << to_string(amt.xrp);
        else
            stream << to_string(amt.iou);
        return stream;
    }
#endif
};

template <class T>
T&
get(EitherAmount& amt)
{
    static_assert(sizeof(T) == -1, "Must used specialized function");
    return T(0);
}

template <>
inline IOUAmount&
get<IOUAmount>(EitherAmount& amt)
{
    ASSERT(!amt.native, "ripple::get<IOUAmount>(EitherAmount&) : is not XRP");
    return amt.iou;
}

template <>
inline XRPAmount&
get<XRPAmount>(EitherAmount& amt)
{
    ASSERT(amt.native, "ripple::get<XRPAmount>(EitherAmount&) : is XRP");
    return amt.xrp;
}

template <class T>
T const&
get(EitherAmount const& amt)
{
    static_assert(sizeof(T) == -1, "Must used specialized function");
    return T(0);
}

template <>
inline IOUAmount const&
get<IOUAmount>(EitherAmount const& amt)
{
    ASSERT(
        !amt.native,
        "ripple::get<IOUAmount>(EitherAmount const&) : is not XRP");
    return amt.iou;
}

template <>
inline XRPAmount const&
get<XRPAmount>(EitherAmount const& amt)
{
    ASSERT(amt.native, "ripple::get<XRPAmount>(EitherAmount const&) : is XRP");
    return amt.xrp;
}

inline AmountSpec
toAmountSpec(STAmount const& amt)
{
    ASSERT(
        amt.mantissa() < std::numeric_limits<std::int64_t>::max(),
        "ripple::toAmountSpec(STAmount const&) : maximum mantissa");
    bool const isNeg = amt.negative();
    std::int64_t const sMant =
        isNeg ? -std::int64_t(amt.mantissa()) : amt.mantissa();
    AmountSpec result;

    result.native = isXRP(amt);
    if (result.native)
    {
        result.xrp = XRPAmount(sMant);
    }
    else
    {
        result.iou = IOUAmount(sMant, amt.exponent());
        result.issuer = amt.issue().account;
        result.currency = amt.issue().currency;
    }

    return result;
}

inline EitherAmount
toEitherAmount(STAmount const& amt)
{
    if (isXRP(amt))
        return EitherAmount{amt.xrp()};
    return EitherAmount{amt.iou()};
}

inline AmountSpec
toAmountSpec(EitherAmount const& ea, std::optional<Currency> const& c)
{
    AmountSpec r;
    r.native = (!c || isXRP(*c));
    r.currency = c;
    ASSERT(
        ea.native == r.native,
        "ripple::toAmountSpec(EitherAmount const&&, std::optional<Currency>) : "
        "matching native");
    if (r.native)
    {
        r.xrp = ea.xrp;
    }
    else
    {
        r.iou = ea.iou;
    }
    return r;
}

}  // namespace ripple

#endif
