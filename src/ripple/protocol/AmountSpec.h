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

#ifndef RIPPLE_PROTOCOL_AMOUNTSPEC_H_INCLUDED
#define RIPPLE_PROTOCOL_AMOUNTSPEC_H_INCLUDED

#include <ripple/protocol/IOUAmount.h>
#include <ripple/protocol/XRPAmount.h>
#include <ripple/protocol/STAmount.h>

namespace ripple {

struct AmountSpec
{
    bool native;
    union
    {
        XRPAmount xrp;
        IOUAmount iou;
    };
    boost::optional<AccountID> issuer;
    boost::optional<Currency> currency;

  private:
    friend
    std::ostream&
    operator << (
        std::ostream& stream,
        AmountSpec const& amt)
    {
        if (amt.native)
            stream << to_string (amt.xrp);
        else
            stream << to_string (amt.iou);
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
        IOUAmount iou;
        XRPAmount xrp;
    };

    EitherAmount () = default;

    explicit
    EitherAmount (IOUAmount const& a)
            :iou(a)
    {
    }

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
    // ignore warning about half of iou amount being uninitialized
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    explicit
    EitherAmount (XRPAmount const& a)
            :xrp(a)
    {
#ifndef NDEBUG
        native = true;
#endif
    }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

    explicit
    EitherAmount (AmountSpec const& a)
    {
#ifndef NDEBUG
        native = a.native;
#endif
        if (a.native)
            xrp = a.xrp;
        else
            iou = a.iou;
    }
};

template <class T>
T&
get (EitherAmount& amt)
{
    static_assert(sizeof(T) == -1, "Must used specialized function");
    return T(0);
}

template <>
inline
IOUAmount&
get<IOUAmount> (EitherAmount& amt)
{
    assert (!amt.native);
    return amt.iou;
}

template <>
inline
XRPAmount&
get<XRPAmount> (EitherAmount& amt)
{
    assert (amt.native);
    return amt.xrp;
}

template <class T>
T const&
get (EitherAmount const& amt)
{
    static_assert(sizeof(T) == -1, "Must used specialized function");
    return T(0);
}

template <>
inline
IOUAmount const&
get<IOUAmount> (EitherAmount const& amt)
{
    assert (!amt.native);
    return amt.iou;
}

template <>
inline
XRPAmount const&
get<XRPAmount> (EitherAmount const& amt)
{
    assert (amt.native);
    return amt.xrp;
}

inline
AmountSpec
toAmountSpec (STAmount const& amt)
{
    assert (amt.mantissa () < std::numeric_limits<std::int64_t>::max ());
    bool const isNeg = amt.negative ();
    std::int64_t const sMant =
        isNeg ? - std::int64_t (amt.mantissa ()) : amt.mantissa ();
    AmountSpec result;

    result.native = isXRP (amt);
    if (result.native)
    {
        result.xrp = XRPAmount (sMant);
    }
    else
    {
        result.iou = IOUAmount (sMant, amt.exponent ());
        result.issuer = amt.issue().account;
        result.currency = amt.issue().currency;
    }

    return result;
}

inline
AmountSpec
toAmountSpec (
    EitherAmount const& ea,
    boost::optional<Currency> const& c)
{
    AmountSpec r;
    r.native = (!c || isXRP (*c));
    r.currency = c;
    assert (ea.native == r.native);
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

inline
STAmount
toSTAmount (IOUAmount const& iou, Issue const& iss)
{
    bool const isNeg = iou.signum() < 0;
    std::uint64_t const umant = isNeg ? - iou.mantissa () : iou.mantissa ();
    return STAmount (iss, umant, iou.exponent (), /*native*/ false, isNeg,
                     STAmount::unchecked ());
}

inline
STAmount
toSTAmount (IOUAmount const& iou)
{
    return toSTAmount (iou, noIssue ());
}

inline
STAmount
toSTAmount (XRPAmount const& xrp)
{
    bool const isNeg = xrp.signum() < 0;
    std::uint64_t const umant = isNeg ? - xrp.drops () : xrp.drops ();
    return STAmount (umant, isNeg);
}

inline
STAmount
toSTAmount (XRPAmount const& xrp, Issue const& iss)
{
    assert (isXRP(iss.account) && isXRP(iss.currency));
    return toSTAmount (xrp);
}

template <class T>
T
toAmount (STAmount const& amt)
{
    static_assert(sizeof(T) == -1, "Must used specialized function");
    return T(0);
}

template <>
inline
STAmount
toAmount<STAmount> (STAmount const& amt)
{
    return amt;
}

template <>
inline
IOUAmount
toAmount<IOUAmount> (STAmount const& amt)
{
    assert (amt.mantissa () < std::numeric_limits<std::int64_t>::max ());
    bool const isNeg = amt.negative ();
    std::int64_t const sMant =
            isNeg ? - std::int64_t (amt.mantissa ()) : amt.mantissa ();

    assert (! isXRP (amt));
    return IOUAmount (sMant, amt.exponent ());
}

template <>
inline
XRPAmount
toAmount<XRPAmount> (STAmount const& amt)
{
    assert (amt.mantissa () < std::numeric_limits<std::int64_t>::max ());
    bool const isNeg = amt.negative ();
    std::int64_t const sMant =
            isNeg ? - std::int64_t (amt.mantissa ()) : amt.mantissa ();
    AmountSpec result;

    assert (isXRP (amt));
    return XRPAmount (sMant);
}


}

#endif
