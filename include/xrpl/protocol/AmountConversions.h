#pragma once

#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/XRPAmount.h>

#include <type_traits>

namespace xrpl {

inline STAmount
toSTAmount(IOUAmount const& iou, Asset const& asset)
{
    XRPL_ASSERT(asset.holds<Issue>(), "xrpl::toSTAmount : is Issue");
    bool const isNeg = iou.signum() < 0;
    std::uint64_t const umant = isNeg ? -iou.mantissa() : iou.mantissa();
    return STAmount(asset, umant, iou.exponent(), isNeg, STAmount::Unchecked());
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
toSTAmount(XRPAmount const& xrp, Asset const& asset)
{
    XRPL_ASSERT(isXRP(asset), "xrpl::toSTAmount : is XRP");
    return toSTAmount(xrp);
}

inline STAmount
toSTAmount(MPTAmount const& mpt)
{
    return STAmount(mpt, noMPT());
}

inline STAmount
toSTAmount(MPTAmount const& mpt, Asset const& asset)
{
    XRPL_ASSERT(asset.holds<MPTIssue>(), "xrpl::toSTAmount : is MPT");
    return STAmount(mpt, asset.get<MPTIssue>());
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
        "xrpl::toAmount<IOUAmount> : maximum mantissa");
    bool const isNeg = amt.negative();
    std::int64_t const sMant = isNeg ? -std::int64_t(amt.mantissa()) : amt.mantissa();

    XRPL_ASSERT(!isXRP(amt), "xrpl::toAmount<IOUAmount> : is not XRP");
    return IOUAmount(sMant, amt.exponent());
}

template <>
inline XRPAmount
toAmount<XRPAmount>(STAmount const& amt)
{
    XRPL_ASSERT(
        amt.mantissa() < std::numeric_limits<std::int64_t>::max(),
        "xrpl::toAmount<XRPAmount> : maximum mantissa");
    bool const isNeg = amt.negative();
    std::int64_t const sMant = isNeg ? -std::int64_t(amt.mantissa()) : amt.mantissa();

    XRPL_ASSERT(isXRP(amt), "xrpl::toAmount<XRPAmount> : is XRP");
    return XRPAmount(sMant);
}

template <>
inline MPTAmount
toAmount<MPTAmount>(STAmount const& amt)
{
    XRPL_ASSERT(
        amt.holds<MPTIssue>() && amt.mantissa() <= kMaxMpTokenAmount && amt.exponent() == 0,
        "xrpl::toAmount<MPTAmount> : maximum mantissa");
    if (amt.mantissa() > kMaxMpTokenAmount || amt.exponent() != 0)
        Throw<std::runtime_error>("toAmount<MPTAmount>: invalid mantissa or exponent");
    bool const isNeg = amt.negative();
    std::int64_t const sMant = isNeg ? -std::int64_t(amt.mantissa()) : amt.mantissa();

    return MPTAmount(sMant);
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

template <class T>
T
toAmount(MPTAmount const& amt) = delete;

template <>
inline MPTAmount
toAmount<MPTAmount>(MPTAmount const& amt)
{
    return amt;
}

template <typename T>
T
toAmount(Asset const& asset, Number const& n, Number::RoundingMode mode = Number::getround())
{
    SaveNumberRoundMode const rm(Number::getround());
    if (isXRP(asset))
        Number::setround(mode);

    if constexpr (std::is_same_v<IOUAmount, T>)
    {
        return IOUAmount(n);
    }
    else if constexpr (std::is_same_v<XRPAmount, T>)
    {
        return XRPAmount(static_cast<std::int64_t>(n));
    }
    else if constexpr (std::is_same_v<MPTAmount, T>)
    {
        return MPTAmount(static_cast<std::int64_t>(n));
    }
    else if constexpr (std::is_same_v<STAmount, T>)
    {
        if (isXRP(asset))
            return STAmount(asset, static_cast<std::int64_t>(n));
        return STAmount(asset, n);
    }
    else
    {
        static constexpr bool kAlwaysFalse = !std::is_same_v<T, T>;
        static_assert(kAlwaysFalse, "Unsupported type for toAmount");
    }
}

template <typename T>
T
toMaxAmount(Asset const& asset)
{
    if constexpr (std::is_same_v<IOUAmount, T>)
    {
        return IOUAmount(STAmount::kMaxValue, STAmount::kMaxOffset);
    }
    else if constexpr (std::is_same_v<XRPAmount, T>)
    {
        return XRPAmount(static_cast<std::int64_t>(STAmount::kMaxNativeN));
    }
    else if constexpr (std::is_same_v<MPTAmount, T>)
    {
        return MPTAmount(kMaxMpTokenAmount);
    }
    else if constexpr (std::is_same_v<STAmount, T>)
    {
        return asset.visit(
            [](Issue const& issue) {
                if (isXRP(issue))
                    return STAmount(issue, static_cast<std::int64_t>(STAmount::kMaxNativeN));
                return STAmount(issue, STAmount::kMaxValue, STAmount::kMaxOffset);
            },
            [](MPTIssue const& issue) { return STAmount(issue, kMaxMpTokenAmount); });
    }
    else
    {
        static constexpr bool kAlwaysFalse = !std::is_same_v<T, T>;
        static_assert(kAlwaysFalse, "Unsupported type for toMaxAmount");
    }
}

inline STAmount
toSTAmount(Asset const& asset, Number const& n, Number::RoundingMode mode = Number::getround())
{
    return toAmount<STAmount>(asset, n, mode);
}

template <typename T>
Asset
getAsset(T const& amt)
{
    if constexpr (std::is_same_v<IOUAmount, T>)
    {
        return noIssue();
    }
    else if constexpr (std::is_same_v<XRPAmount, T>)
    {
        return xrpIssue();
    }
    else if constexpr (std::is_same_v<MPTAmount, T>)
    {
        return noMPT();
    }
    else if constexpr (std::is_same_v<STAmount, T>)
    {
        return amt.asset();
    }
    else
    {
        static constexpr bool kAlwaysFalse = !std::is_same_v<T, T>;
        static_assert(kAlwaysFalse, "Unsupported type for getIssue");
    }
}

template <typename T>
constexpr T
get(STAmount const& a)
{
    if constexpr (std::is_same_v<IOUAmount, T>)
    {
        return a.iou();
    }
    else if constexpr (std::is_same_v<XRPAmount, T>)
    {
        return a.xrp();
    }
    else if constexpr (std::is_same_v<MPTAmount, T>)
    {
        return a.mpt();
    }
    else if constexpr (std::is_same_v<STAmount, T>)
    {
        return a;
    }
    else
    {
        constexpr bool kAlwaysFalse = !std::is_same_v<T, T>;
        static_assert(kAlwaysFalse, "Unsupported type for get");
    }
}

}  // namespace xrpl
