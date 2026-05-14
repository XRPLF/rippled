#pragma once

/** @file
 *  Conversion utilities between the four XRPL amount representations.
 *
 *  The protocol defines four amount types, each optimized for a different
 *  concern: `XRPAmount` (integer drops), `MPTAmount` (integer MPT units),
 *  `IOUAmount` (normalized floating-point), and `STAmount` (wire-level union
 *  over all three). Generic algorithms — AMM pricing, pathfinding, offer
 *  crossing — need to work across all four without duplicating logic. This
 *  header provides the glue: inline conversion functions that move freely
 *  between representations. No arithmetic or business logic lives here.
 */

#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/XRPAmount.h>

#include <type_traits>

namespace xrpl {

/** Wrap an `IOUAmount` in a serializable `STAmount` tagged with the given asset.
 *
 *  `IOUAmount` stores a signed mantissa; `STAmount` stores an unsigned mantissa
 *  with a separate sign bit. This overload performs that split manually and
 *  constructs via `STAmount::Unchecked()` to skip re-canonicalization —
 *  `IOUAmount` is already normalized so re-canonicalizing would be wasted work.
 *
 *  @param iou The IOU value to wrap.
 *  @param asset The asset identity to embed; must hold an `Issue` (not XRP
 *      or MPT) — verified by assertion.
 *  @return An `STAmount` encoding the same value and sign as `iou`.
 *  @note Passing an XRP or MPT asset silently produces wrong data in release
 *      builds; the assertion catches this only in debug builds.
 */
inline STAmount
toSTAmount(IOUAmount const& iou, Asset const& asset)
{
    XRPL_ASSERT(asset.holds<Issue>(), "xrpl::toSTAmount : is Issue");
    bool const isNeg = iou.signum() < 0;
    std::uint64_t const umant = isNeg ? -iou.mantissa() : iou.mantissa();
    return STAmount(asset, umant, iou.exponent(), isNeg, STAmount::Unchecked());
}

/** Wrap an `IOUAmount` in an `STAmount` with a placeholder `noIssue()` asset.
 *
 *  Convenience overload for contexts where the true asset identity is not
 *  available at the call site. The resulting `STAmount` carries `noIssue()`
 *  as its asset tag and should not be used in wire serialization.
 *
 *  @param iou The IOU value to wrap.
 *  @return An `STAmount` encoding the value of `iou` with `noIssue()` asset.
 */
inline STAmount
toSTAmount(IOUAmount const& iou)
{
    return toSTAmount(iou, noIssue());
}

/** Wrap an `XRPAmount` in a serializable `STAmount`.
 *
 *  @param xrp The XRP drop count to wrap; may be negative.
 *  @return A native `STAmount` encoding the same value and sign as `xrp`.
 */
inline STAmount
toSTAmount(XRPAmount const& xrp)
{
    bool const isNeg = xrp.signum() < 0;
    std::uint64_t const umant = isNeg ? -xrp.drops() : xrp.drops();
    return STAmount(umant, isNeg);
}

/** Wrap an `XRPAmount` in an `STAmount` given an explicit `Asset`.
 *
 *  Exists to give generic code a uniform `toSTAmount(amount, asset)` call
 *  signature; delegates immediately to the asset-less overload after asserting
 *  that `asset` is XRP.
 *
 *  @param xrp The XRP drop count to wrap.
 *  @param asset Must be the XRP asset — verified by assertion.
 *  @return A native `STAmount` encoding the same value as `xrp`.
 */
inline STAmount
toSTAmount(XRPAmount const& xrp, Asset const& asset)
{
    XRPL_ASSERT(isXRP(asset), "xrpl::toSTAmount : is XRP");
    return toSTAmount(xrp);
}

/** Wrap an `MPTAmount` in an `STAmount` with a placeholder `noMPT()` asset.
 *
 *  @param mpt The MPT unit count to wrap.
 *  @return An `STAmount` encoding the value of `mpt` with `noMPT()` asset.
 */
inline STAmount
toSTAmount(MPTAmount const& mpt)
{
    return STAmount(mpt, noMPT());
}

/** Wrap an `MPTAmount` in an `STAmount` tagged with the given MPT asset.
 *
 *  @param mpt The MPT unit count to wrap.
 *  @param asset The asset identity to embed; must hold an `MPTIssue` —
 *      verified by assertion.
 *  @return An `STAmount` encoding the value of `mpt` with the given
 *      `MPTIssue` identity.
 */
inline STAmount
toSTAmount(MPTAmount const& mpt, Asset const& asset)
{
    XRPL_ASSERT(asset.holds<MPTIssue>(), "xrpl::toSTAmount : is MPT");
    return STAmount(mpt, asset.get<MPTIssue>());
}

/** Primary template for `STAmount` → lean-type extraction; intentionally deleted.
 *
 *  Calling `toAmount<T>(stamt)` with an unsupported `T` is a hard compile
 *  error rather than a linker error or silent mis-conversion. Only the
 *  explicit specializations below (`STAmount`, `IOUAmount`, `XRPAmount`,
 *  `MPTAmount`) are valid.
 *
 *  @tparam T Target amount type.
 */
template <class T>
T
toAmount(STAmount const& amt) = delete;

/** Identity conversion: return the `STAmount` unchanged.
 *
 *  @param amt The `STAmount` to return.
 *  @return `amt` unchanged.
 */
template <>
inline STAmount
toAmount<STAmount>(STAmount const& amt)
{
    return amt;
}

/** Extract the IOU value from an `STAmount` as an `IOUAmount`.
 *
 *  Reconstitutes the signed mantissa (STAmount stores it unsigned + sign bit)
 *  and constructs an `IOUAmount` directly without re-canonicalization.
 *
 *  @param amt The source `STAmount`; must not be a native XRP amount —
 *      verified by assertion. Mantissa must fit in `int64_t` — verified
 *      by assertion.
 *  @return An `IOUAmount` with the same signed mantissa and exponent.
 */
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

/** Extract the XRP drop count from an `STAmount` as an `XRPAmount`.
 *
 *  @param amt The source `STAmount`; must be a native XRP amount —
 *      verified by assertion. Mantissa must fit in `int64_t` — verified
 *      by assertion.
 *  @return An `XRPAmount` holding the signed drop count.
 */
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

/** Extract the MPT unit count from an `STAmount` as an `MPTAmount`.
 *
 *  MPT amounts are integers: the exponent must be exactly 0 and the
 *  mantissa must not exceed `kMAX_MP_TOKEN_AMOUNT`. Both constraints are
 *  checked in debug builds (assertion) and in release builds (exception),
 *  because a violation indicates data corruption or a ledger encoding bug
 *  that should surface loudly rather than silently truncate.
 *
 *  @param amt The source `STAmount`; must hold an `MPTIssue`, have exponent
 *      0, and mantissa ≤ `kMAX_MP_TOKEN_AMOUNT`.
 *  @return An `MPTAmount` holding the signed unit count.
 *  @throws std::runtime_error if `amt.exponent() != 0` or
 *      `amt.mantissa() > kMAX_MP_TOKEN_AMOUNT`.
 */
template <>
inline MPTAmount
toAmount<MPTAmount>(STAmount const& amt)
{
    XRPL_ASSERT(
        amt.holds<MPTIssue>() && amt.mantissa() <= kMAX_MP_TOKEN_AMOUNT && amt.exponent() == 0,
        "xrpl::toAmount<MPTAmount> : maximum mantissa");
    if (amt.mantissa() > kMAX_MP_TOKEN_AMOUNT || amt.exponent() != 0)
        Throw<std::runtime_error>("toAmount<MPTAmount>: invalid mantissa or exponent");
    bool const isNeg = amt.negative();
    std::int64_t const sMant = isNeg ? -std::int64_t(amt.mantissa()) : amt.mantissa();

    return MPTAmount(sMant);
}

/** Primary template for `IOUAmount` → same-type extraction; intentionally deleted.
 *
 *  Only the `IOUAmount` identity specialization below is valid.
 *
 *  @tparam T Target amount type.
 */
template <class T>
T
toAmount(IOUAmount const& amt) = delete;

/** Identity conversion: return the `IOUAmount` unchanged.
 *
 *  Allows generic code to call `toAmount<IOUAmount>(iouValue)` without
 *  branching on whether the source is already the target type.
 *
 *  @param amt The `IOUAmount` to return.
 *  @return `amt` unchanged.
 */
template <>
inline IOUAmount
toAmount<IOUAmount>(IOUAmount const& amt)
{
    return amt;
}

/** Primary template for `XRPAmount` → same-type extraction; intentionally deleted.
 *
 *  Only the `XRPAmount` identity specialization below is valid.
 *
 *  @tparam T Target amount type.
 */
template <class T>
T
toAmount(XRPAmount const& amt) = delete;

/** Identity conversion: return the `XRPAmount` unchanged.
 *
 *  Allows generic code to call `toAmount<XRPAmount>(xrpValue)` without
 *  branching on whether the source is already the target type.
 *
 *  @param amt The `XRPAmount` to return.
 *  @return `amt` unchanged.
 */
template <>
inline XRPAmount
toAmount<XRPAmount>(XRPAmount const& amt)
{
    return amt;
}

/** Primary template for `MPTAmount` → same-type extraction; intentionally deleted.
 *
 *  Only the `MPTAmount` identity specialization below is valid.
 *
 *  @tparam T Target amount type.
 */
template <class T>
T
toAmount(MPTAmount const& amt) = delete;

/** Identity conversion: return the `MPTAmount` unchanged.
 *
 *  Allows generic code to call `toAmount<MPTAmount>(mptValue)` without
 *  branching on whether the source is already the target type.
 *
 *  @param amt The `MPTAmount` to return.
 *  @return `amt` unchanged.
 */
template <>
inline MPTAmount
toAmount<MPTAmount>(MPTAmount const& amt)
{
    return amt;
}

/** Convert a `Number` intermediate result to a typed amount, applying a
 *  caller-specified rounding mode for XRP.
 *
 *  Used by AMM pricing and pathfinding after performing arithmetic in
 *  `Number` space. The rounding mode override is applied **only for XRP**:
 *  XRP is an integer count of drops, so converting a rational intermediate
 *  requires a deterministic rounding decision. IOU and MPT types handle
 *  normalization internally and do not require external rounding control.
 *  `SaveNumberRoundMode` restores the previous thread-local rounding mode
 *  on destruction, even if the conversion throws.
 *
 *  @tparam T Target amount type: `IOUAmount`, `XRPAmount`, `MPTAmount`, or
 *      `STAmount`. Any other type is a compile error.
 *  @param asset The asset identity for the result; must be consistent with
 *      `T` (e.g., XRP asset with `XRPAmount`).
 *  @param n The intermediate `Number` value to convert.
 *  @param mode Rounding mode applied when `T` is `XRPAmount` or when
 *      `T` is `STAmount` with an XRP asset. Defaults to the current
 *      thread-local rounding mode.
 *  @return The converted amount of type `T`.
 */
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
        constexpr bool kALWAYS_FALSE = !std::is_same_v<T, T>;
        static_assert(kALWAYS_FALSE, "Unsupported type for toAmount");
    }
}

/** Return the maximum representable value for a given amount type and asset.
 *
 *  Dispatches at compile time on `T`. For `STAmount` the result depends on
 *  the runtime asset: XRP uses `kMAX_NATIVE_N` drops; IOU uses
 *  `(kMAX_VALUE, kMAX_OFFSET)`; MPT uses `kMAX_MP_TOKEN_AMOUNT`.
 *
 *  @tparam T Target amount type: `IOUAmount`, `XRPAmount`, `MPTAmount`, or
 *      `STAmount`. Any other type is a compile error.
 *  @param asset The asset identity; consulted only when `T` is `STAmount`.
 *  @return The maximum representable value of type `T`.
 */
template <typename T>
T
toMaxAmount(Asset const& asset)
{
    if constexpr (std::is_same_v<IOUAmount, T>)
    {
        return IOUAmount(STAmount::kMAX_VALUE, STAmount::kMAX_OFFSET);
    }
    else if constexpr (std::is_same_v<XRPAmount, T>)
    {
        return XRPAmount(static_cast<std::int64_t>(STAmount::kMAX_NATIVE_N));
    }
    else if constexpr (std::is_same_v<MPTAmount, T>)
    {
        return MPTAmount(kMAX_MP_TOKEN_AMOUNT);
    }
    else if constexpr (std::is_same_v<STAmount, T>)
    {
        return asset.visit(
            [](Issue const& issue) {
                if (isXRP(issue))
                    return STAmount(issue, static_cast<std::int64_t>(STAmount::kMAX_NATIVE_N));
                return STAmount(issue, STAmount::kMAX_VALUE, STAmount::kMAX_OFFSET);
            },
            [](MPTIssue const& issue) { return STAmount(issue, kMAX_MP_TOKEN_AMOUNT); });
    }
    else
    {
        constexpr bool kALWAYS_FALSE = !std::is_same_v<T, T>;
        static_assert(kALWAYS_FALSE, "Unsupported type for toMaxAmount");
    }
}

/** Convert a `Number` intermediate to an `STAmount` with a given asset and rounding mode.
 *
 *  Thin wrapper around `toAmount<STAmount>(asset, n, mode)` provided so
 *  callers that always work with `STAmount` can use a non-template name.
 *
 *  @param asset The asset identity for the result.
 *  @param n The intermediate `Number` value to convert.
 *  @param mode Rounding mode applied when `asset` is XRP. Defaults to the
 *      current thread-local rounding mode.
 *  @return An `STAmount` encoding `n` tagged with `asset`.
 *  @see toAmount
 */
inline STAmount
toSTAmount(Asset const& asset, Number const& n, Number::RoundingMode mode = Number::getround())
{
    return toAmount<STAmount>(asset, n, mode);
}

/** Return a placeholder `Asset` for a given amount type.
 *
 *  For `STAmount` this delegates to `amt.asset()` and returns the true asset.
 *  For lean types — `IOUAmount`, `XRPAmount`, `MPTAmount` — which do not
 *  carry asset identity, a sentinel is returned: `noIssue()`, `xrpIssue()`,
 *  or `noMPT()` respectively. Callers such as AMM helpers use this to
 *  produce an `Asset` argument for a subsequent `toAmount<T>(asset, ...)` call
 *  where the true asset is known from context.
 *
 *  @tparam T Source amount type. Any other type is a compile error.
 *  @param amt The amount whose asset identity is requested.
 *  @return The true `Asset` for `STAmount`; a type-appropriate sentinel
 *      for lean types.
 */
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
        constexpr bool kALWAYS_FALSE = !std::is_same_v<T, T>;
        static_assert(kALWAYS_FALSE, "Unsupported type for getIssue");
    }
}

/** Extract a typed value from an `STAmount` by delegating to the
 *  appropriate accessor.
 *
 *  Dispatches at compile time: `IOUAmount` → `a.iou()`, `XRPAmount` →
 *  `a.xrp()`, `MPTAmount` → `a.mpt()`, `STAmount` → identity. The
 *  `static_assert` in the else branch uses a type-dependent expression
 *  so it fires only when the unsupported branch is actually instantiated,
 *  not on every parse of the template.
 *
 *  @tparam T Target lean type or `STAmount`. Any other type is a compile error.
 *  @param a The source `STAmount`.
 *  @return The value of `a` expressed as type `T`.
 */
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
        constexpr bool kALWAYS_FALSE = !std::is_same_v<T, T>;
        static_assert(kALWAYS_FALSE, "Unsupported type for get");
    }
}

}  // namespace xrpl
