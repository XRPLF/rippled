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

#include <ripple/basics/CFTAmount.h>
#include <ripple/basics/IOUAmount.h>
#include <ripple/basics/XRPAmount.h>
#include <ripple/protocol/STAmount.h>

#include <optional>

namespace ripple {

struct AmountSpec
{
    explicit AmountSpec() = default;

    bool native;
    bool is_cft;
    union
    {
        CFTAmount cft;
        XRPAmount xrp;
        IOUAmount iou = {};
    };
    std::optional<AccountID> issuer;
    std::optional<Asset> currency;

    friend std::ostream&
    operator<<(std::ostream& stream, AmountSpec const& amt)
    {
        if (amt.is_cft)
            stream << to_string(amt.cft);
        else if (amt.native)
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
    bool is_cft = false;
#endif

    union
    {
        IOUAmount iou = {};
        XRPAmount xrp;
        CFTAmount cft;
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

    explicit EitherAmount(CFTAmount const& a) : cft(a)
    {
#ifndef NDEBUG
        is_cft = true;
#endif
    }

    explicit EitherAmount(AmountSpec const& a)
    {
#ifndef NDEBUG
        native = a.native;
        is_cft = a.is_cft;
#endif
        if (a.is_cft)
            cft = a.cft;
        else if (a.native)
            xrp = a.xrp;
        else
            iou = a.iou;
    }

#ifndef NDEBUG
    friend std::ostream&
    operator<<(std::ostream& stream, EitherAmount const& amt)
    {
        if (amt.is_cft)
            stream << to_string(amt.cft);
        else if (amt.native)
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
    assert(!amt.native && !amt.is_cft);
    return amt.iou;
}

template <>
inline XRPAmount&
get<XRPAmount>(EitherAmount& amt)
{
    assert(amt.native && !amt.is_cft);
    return amt.xrp;
}

template <>
inline CFTAmount&
get<CFTAmount>(EitherAmount& amt)
{
    assert(amt.is_cft && !amt.native);
    return amt.cft;
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
    assert(!amt.native && !amt.is_cft);
    return amt.iou;
}

template <>
inline XRPAmount const&
get<XRPAmount>(EitherAmount const& amt)
{
    assert(amt.native && !amt.is_cft);
    return amt.xrp;
}

template <>
inline CFTAmount const&
get<CFTAmount>(EitherAmount const& amt)
{
    assert(amt.is_cft && !amt.native);
    return amt.cft;
}

inline AmountSpec
toAmountSpec(STAmount const& amt)
{
    assert(amt.mantissa() < std::numeric_limits<std::int64_t>::max());
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
        if (amt.isCFT())
            result.cft = CFTAmount(sMant);
        else
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
    else if (amt.isCFT())
        return EitherAmount{amt.cft()};
    return EitherAmount{amt.iou()};
}

inline AmountSpec
toAmountSpec(EitherAmount const& ea, std::optional<Asset> const& a)
{
    AmountSpec r;
    r.native = (!a || isXRP(*a));
    r.currency = a;
    assert(ea.native == r.native);
    if (r.is_cft)
    {
        r.cft = ea.cft;
    }
    else if (r.native)
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
