#pragma once

/** @file
 *  Sentinel-value utilities for the "convert all available liquidity" mode
 *  of the XRPL pathfinding subsystem.
 *
 *  The pathfinder supports a mode where the sender wants to drain the maximum
 *  available liquidity through discovered paths rather than target a fixed
 *  destination amount.  This is signalled at the RPC layer by setting the
 *  destination amount to the largest representable value for its asset type —
 *  a sentinel value rather than a separate flag.  These three inline helpers
 *  construct, apply, and detect that sentinel.
 */

#include <xrpl/protocol/STAmount.h>

namespace xrpl {

/** Construct the maximum representable sentinel amount for the asset of @p amt.
 *
 *  The sentinel is asset-type–specific because XRP, IOU, and MPT each use a
 *  different numeric encoding:
 *
 *  - **XRP**: `kINITIAL_XRP` (10^17 drops — the entire initial supply and the
 *      largest valid XRP amount by protocol convention).
 *  - **IOU**: mantissa `kMAX_VALUE` (9,999,999,999,999,999) with exponent
 *      `kMAX_OFFSET` (80), yielding approximately 10^96 — effectively
 *      unbounded for any realistic liquidity.
 *  - **MPT**: mantissa `kMAX_MP_TOKEN_AMOUNT` (2^63−1) with exponent 0.
 *      MPTs use integer rather than floating-point encoding, so the IOU
 *      boundary values do not apply.
 *
 *  @param amt  An amount whose asset type determines which sentinel is built.
 *      The magnitude of @p amt itself is ignored; only `amt.asset()` matters.
 *  @return The maximum representable `STAmount` for the same asset as @p amt.
 *  @note A legitimate request for the exact maximum representable amount is
 *      indistinguishable from the "convert all" intent.  In practice this is
 *      harmless because no real payment specifies `kINITIAL_XRP` or IOU
 *      `kMAX_VALUE` as an exact destination.
 *  @see convertAmount, convertAllCheck
 */
inline STAmount
largestAmount(STAmount const& amt)
{
    return amt.asset().visit(
        [&](Issue const& issue) -> STAmount {
            if (issue.native())
                return kINITIAL_XRP;
            return STAmount(amt.asset(), STAmount::kMAX_VALUE, STAmount::kMAX_OFFSET);
        },
        [&](MPTIssue const&) { return STAmount(amt.asset(), kMAX_MP_TOKEN_AMOUNT, 0); });
}

/** Return the effective destination amount for a pathfinding computation.
 *
 *  When @p all is `false` the original amount passes through unchanged.
 *  When @p all is `true` the amount is replaced by the asset-appropriate
 *  sentinel from `largestAmount`, signalling to `RippleCalc` that it should
 *  seek maximum liquidity rather than an exact fill.
 *
 *  Called by `PathRequest::doUpdate()` and `Pathfinder::computePathRanks()`
 *  to select the destination amount fed into `RippleCalc`.
 *
 *  @param amt  The requested destination amount.
 *  @param all  `true` to substitute the "convert all" sentinel; `false` to
 *      return @p amt unchanged.
 *  @return @p amt when @p all is `false`; otherwise `largestAmount(amt)`.
 *  @see largestAmount, convertAllCheck
 */
inline STAmount
convertAmount(STAmount const& amt, bool all)
{
    if (!all)
        return amt;

    return largestAmount(amt);
};

/** Detect whether an amount is already the "convert all" sentinel.
 *
 *  Compares @p a against `largestAmount(a)` to determine whether it carries
 *  the maximum-liquidity sentinel for its asset type.  Used in
 *  `Pathfinder`'s constructor to initialise `convert_all_`:
 *
 *  ```cpp
 *  convert_all_(convertAllCheck(mDstAmount))
 *  ```
 *
 *  When `convert_all_` is `true`, `partialPaymentAllowed` is set in the
 *  `RippleCalc` input so the engine maximises delivered amount rather than
 *  insisting on an exact fill, and path ranking biases toward high-liquidity
 *  paths by using `largestAmount` as the minimum acceptable destination.
 *
 *  @param a  The destination amount to test.
 *  @return `true` if @p a equals the sentinel for its asset type; `false`
 *      otherwise.
 *  @see largestAmount, convertAmount
 */
inline bool
convertAllCheck(STAmount const& a)
{
    return a == largestAmount(a);
}

}  // namespace xrpl
