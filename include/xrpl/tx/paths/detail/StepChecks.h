/** @file
 *  Freeze and NoRipple compliance guards for payment path steps.
 *
 *  Every candidate step through the XRPL payment engine must pass both
 *  `checkFreeze` and `checkNoRipple` before the engine may use it. Both
 *  functions are read-only probes against the ledger state; neither modifies
 *  any entry. They are inlined here so each step-type translation unit
 *  embeds its own copy, avoiding call overhead on the hot path of payment
 *  execution.
 */

#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

/** Determine whether a trust-line hop is blocked by any freeze mechanism.
 *
 *  Tests three freeze layers in order, returning `terNO_LINE` at the first
 *  violation:
 *
 *  1. **Global freeze** — if `dst` has `lsfGlobalFreeze` set, all IOUs it
 *     issues are inaccessible regardless of per-line flags.
 *  2. **Directional per-line freeze** — the `dst`-side freeze flag
 *     (`lsfHighFreeze` or `lsfLowFreeze`, selected by canonical high/low
 *     ordering) blocks the step. This is asymmetric: an issuer may freeze a
 *     customer's line without affecting its own ability to redeem.
 *  3. **Deep freeze** — either side's `lsfHighDeepFreeze`/`lsfLowDeepFreeze`
 *     bit blocks movement in both directions unconditionally.
 *
 *  When `fixFrozenLPTokenTransfer` is active a fourth check applies: if
 *  `dst` is an AMM account (`sfAMMID` present), the corresponding AMM object
 *  is read and `isLPTokenFrozen()` tests whether the underlying pool assets
 *  are frozen. A missing AMM entry returns `tecINTERNAL` (indicates ledger
 *  corruption; marked `LCOV_EXCL_LINE`).
 *
 *  @note In `DirectStep`, this check is intentionally skipped when the step
 *      is simultaneously first and last (`ctx.isFirst && ctx.isLast`),
 *      because a pure issue/redeem between the transaction's ultimate source
 *      and destination is inherently authorized and cannot be frozen.
 *
 *  @param view The read-only ledger view.
 *  @param src The source account of this hop.
 *  @param dst The destination account of this hop (typically the IOU issuer
 *      whose freeze flags are authoritative for the line).
 *  @param currency The currency flowing across the trust line.
 *  @return `tesSUCCESS` if the hop is unblocked; `terNO_LINE` if any freeze
 *      layer blocks it; `tecINTERNAL` on AMM ledger corruption.
 *  @pre `src != dst` — self-loops must be excluded by the caller before
 *      invoking this function.
 */
inline TER
checkFreeze(
    ReadView const& view,
    AccountID const& src,
    AccountID const& dst,
    Currency const& currency)
{
    XRPL_ASSERT(src != dst, "xrpl::checkFreeze : unequal input accounts");

    // check freeze
    if (auto sle = view.read(keylet::account(dst)))
    {
        if (sle->isFlag(lsfGlobalFreeze))
        {
            return terNO_LINE;
        }
    }

    if (auto sle = view.read(keylet::line(src, dst, currency)))
    {
        if (sle->isFlag((dst > src) ? lsfHighFreeze : lsfLowFreeze))
        {
            return terNO_LINE;
        }
        // Unlike normal freeze, a deep frozen trust line acts the same
        // regardless of which side froze it
        if (sle->isFlag(lsfHighDeepFreeze) || sle->isFlag(lsfLowDeepFreeze))
        {
            return terNO_LINE;
        }
    }

    if (view.rules().enabled(fixFrozenLPTokenTransfer))
    {
        if (auto const sleDst = view.read(keylet::account(dst));
            sleDst && sleDst->isFieldPresent(sfAMMID))
        {
            auto const sleAmm = view.read(keylet::amm((*sleDst)[sfAMMID]));
            if (!sleAmm)
                return tecINTERNAL;  // LCOV_EXCL_LINE

            if (isLPTokenFrozen(view, src, (*sleAmm)[sfAsset], (*sleAmm)[sfAsset2]))
            {
                return terNO_LINE;
            }
        }
    }

    return tesSUCCESS;
}

/** Determine whether the NoRipple setting on an intermediate account blocks
 *  a payment path through it.
 *
 *  Evaluates the triple (`prev` → `cur` → `next`): transit through `cur` is
 *  rejected only when **both** of `cur`'s trust lines — the incoming line from
 *  `prev` and the outgoing line to `next` — carry `cur`'s NoRipple flag. The
 *  correct flag bit (`lsfHighNoRipple` or `lsfLowNoRipple`) is selected for
 *  each line using the canonical high/low account ordering.
 *
 *  The AND semantics are deliberate: an intermediate account may have NoRipple
 *  enabled on some lines (to isolate certain counterparties) while leaving
 *  others open to routing. Transit is permitted as long as at least one side
 *  of the path through `cur` is "open."
 *
 *  Returns `terNO_LINE` immediately if either trust line is absent — there is
 *  nothing to route through. A diagnostic message at `info` level is logged
 *  when `terNO_RIPPLE` is returned, naming all three accounts involved, to aid
 *  in tracing pathfinding failures.
 *
 *  @param view The read-only ledger view.
 *  @param prev The account on the incoming side of `cur`.
 *  @param cur The intermediate account whose NoRipple constraints are checked.
 *  @param next The account on the outgoing side of `cur`.
 *  @param currency The currency flowing through both trust lines.
 *  @param j Journal used to log a diagnostic when the path is rejected.
 *  @return `tesSUCCESS` if transit through `cur` is permitted; `terNO_LINE`
 *      if either trust line does not exist; `terNO_RIPPLE` if both lines have
 *      NoRipple set from `cur`'s perspective.
 */
inline TER
checkNoRipple(
    ReadView const& view,
    AccountID const& prev,
    AccountID const& cur,
    // This is the account whose constraints we are checking
    AccountID const& next,
    Currency const& currency,
    beast::Journal j)
{
    // fetch the ripple lines into and out of this node
    auto sleIn = view.read(keylet::line(prev, cur, currency));
    auto sleOut = view.read(keylet::line(cur, next, currency));

    if (!sleIn || !sleOut)
        return terNO_LINE;

    if ((((*sleIn)[sfFlags] & ((cur > prev) ? lsfHighNoRipple : lsfLowNoRipple)) != 0u) &&
        (((*sleOut)[sfFlags] & ((cur > next) ? lsfHighNoRipple : lsfLowNoRipple)) != 0u))
    {
        JLOG(j.info()) << "Path violates noRipple constraint between " << prev << ", " << cur
                       << " and " << next;

        return terNO_RIPPLE;
    }
    return tesSUCCESS;
}

}  // namespace xrpl
