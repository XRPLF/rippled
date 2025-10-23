#ifndef XRPL_APP_PATHS_IMPL_STEP_CHECKS_H_INCLUDED
#define XRPL_APP_PATHS_IMPL_STEP_CHECKS_H_INCLUDED

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/UintTypes.h>

namespace ripple {

inline TER
checkFreeze(
    ReadView const& view,
    AccountID const& src,
    AccountID const& dst,
    Currency const& currency)
{
    XRPL_ASSERT(src != dst, "ripple::checkFreeze : unequal input accounts");

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

            if (isLPTokenFrozen(
                    view,
                    src,
                    (*sleAmm)[sfAsset].get<Issue>(),
                    (*sleAmm)[sfAsset2].get<Issue>()))
            {
                return terNO_LINE;
            }
        }
    }

    return tesSUCCESS;
}

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

    if ((*sleIn)[sfFlags] & ((cur > prev) ? lsfHighNoRipple : lsfLowNoRipple) &&
        (*sleOut)[sfFlags] & ((cur > next) ? lsfHighNoRipple : lsfLowNoRipple))
    {
        JLOG(j.info()) << "Path violates noRipple constraint between " << prev
                       << ", " << cur << " and " << next;

        return terNO_RIPPLE;
    }
    return tesSUCCESS;
}

}  // namespace ripple

#endif
