#ifndef XRPL_PROTOCOL_AMOUNTCONVERSION_H_INCLUDED
#define XRPL_PROTOCOL_AMOUNTCONVERSION_H_INCLUDED

#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/XRPAmount.h>

#include <type_traits>

namespace ripple {

inline STAmount
toSTAmount(IOUAmount const& iou, Issue const& iss)
{
    bool const isNeg = iou.signum() < 0;
    std::uint64_t const umant = isNeg ? -iou.mantissa() : iou.mantissa();
    return STAmount(iss, umant, iou.exponent(), isNeg, STAmount::unchecked());
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
    XRPL_ASSERT(
        isXRP(iss.account) && isXRP(iss.currency),
        "ripple::toSTAmount : is XRP");
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
    XRPL_ASSERT(
        amt.mantissa() < std::numeric_limits<std::int64_t>::max(),
        "ripple::toAmount<IOUAmount> : maximum mantissa");
    bool const isNeg = amt.negative();
    std::int64_t const sMant =
        isNeg ? -std::int64_t(amt.mantissa()) : amt.mantissa();

    XRPL_ASSERT(!isXRP(amt), "ripple::toAmount<IOUAmount> : is not XRP");
    return IOUAmount(sMant, amt.exponent());
}

template <>
inline XRPAmount
toAmount<XRPAmount>(STAmount const& amt)
{
    XRPL_ASSERT(
        amt.mantissa() < std::numeric_limits<std::int64_t>::max(),
        "ripple::toAmount<XRPAmount> : maximum mantissa");
    bool const isNeg = amt.negative();
    std::int64_t const sMant =
        isNeg ? -std::int64_t(amt.mantissa()) : amt.mantissa();

    XRPL_ASSERT(isXRP(amt), "ripple::toAmount<XRPAmount> : is XRP");
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
    else if constexpr (std::is_same_v<XRPAmount, T>)
        return XRPAmount(static_cast<std::int64_t>(n));
    else if constexpr (std::is_same_v<STAmount, T>)
    {
        if (isXRP(issue))
            return STAmount(issue, static_cast<std::int64_t>(n));
        return STAmount(issue, n.mantissa(), n.exponent());
    }
    else
    {
        constexpr bool alwaysFalse = !std::is_same_v<T, T>;
        static_assert(alwaysFalse, "Unsupported type for toAmount");
    }
}

template <typename T>
T
toMaxAmount(Issue const& issue)
{
    if constexpr (std::is_same_v<IOUAmount, T>)
        return IOUAmount(STAmount::cMaxValue, STAmount::cMaxOffset);
    else if constexpr (std::is_same_v<XRPAmount, T>)
        return XRPAmount(static_cast<std::int64_t>(STAmount::cMaxNativeN));
    else if constexpr (std::is_same_v<STAmount, T>)
    {
        if (isXRP(issue))
            return STAmount(
                issue, static_cast<std::int64_t>(STAmount::cMaxNativeN));
        return STAmount(issue, STAmount::cMaxValue, STAmount::cMaxOffset);
    }
    else
    {
        constexpr bool alwaysFalse = !std::is_same_v<T, T>;
        static_assert(alwaysFalse, "Unsupported type for toMaxAmount");
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
    else if constexpr (std::is_same_v<XRPAmount, T>)
        return xrpIssue();
    else if constexpr (std::is_same_v<STAmount, T>)
        return amt.issue();
    else
    {
        constexpr bool alwaysFalse = !std::is_same_v<T, T>;
        static_assert(alwaysFalse, "Unsupported type for getIssue");
    }
}

template <typename T>
constexpr T
get(STAmount const& a)
{
    if constexpr (std::is_same_v<IOUAmount, T>)
        return a.iou();
    else if constexpr (std::is_same_v<XRPAmount, T>)
        return a.xrp();
    else if constexpr (std::is_same_v<STAmount, T>)
        return a;
    else
    {
        constexpr bool alwaysFalse = !std::is_same_v<T, T>;
        static_assert(alwaysFalse, "Unsupported type for get");
    }
}

}  // namespace ripple

#endif
