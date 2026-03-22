#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/entries/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

inline TER
checkFreeze(
    ReadView const& view,
    AccountID const& src,
    AccountID const& dst,
    Currency const& currency)
{
    XRPL_ASSERT(src != dst, "xrpl::checkFreeze : unequal input accounts");

    // check freeze
    AccountRoot const acctDst(dst, view);
    if (acctDst)
    {
        if (acctDst->isFlag(lsfGlobalFreeze))
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
        if (AccountRoot const acctDst2(dst, view); acctDst2 && acctDst2->isFieldPresent(sfAMMID))
        {
            auto const sleAmm = view.read(keylet::amm(acctDst2->at(sfAMMID)));
            if (!sleAmm)
                return tecINTERNAL;  // LCOV_EXCL_LINE

            if (isLPTokenFrozen(
                    view, src, (*sleAmm)[sfAsset].get<Issue>(), (*sleAmm)[sfAsset2].get<Issue>()))
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
        JLOG(j.info()) << "Path violates noRipple constraint between " << prev << ", " << cur
                       << " and " << next;

        return terNO_RIPPLE;
    }
    return tesSUCCESS;
}

}  // namespace xrpl
