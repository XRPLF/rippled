//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2015 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_PATHS_IMPL_STEP_CHECKS_H_INCLUDED
#define RIPPLE_APP_PATHS_IMPL_STEP_CHECKS_H_INCLUDED

#include <xrpld/ledger/ReadView.h>
#include <xrpld/ledger/View.h>
#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/UintTypes.h>
#include "xrpl/protocol/TER.h"

namespace ripple {

inline TER
checkFreeze(
    ReadView const& view,
    AccountID const& src,
    AccountID const& dst,
    Currency const& currency)
{
    assert(src != dst);

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
    }

    if (view.rules().enabled(fixLPTokenTransfer))
    {
        if (auto const sleDst = view.read(keylet::account(dst));
            sleDst && sleDst->isFieldPresent(sfAMMID))
        {
            auto const sleAmm = view.read(keylet::amm((*sleDst)[sfAMMID]));
            if (!sleAmm)
                return tecINTERNAL;

            if (isLPTokenFrozen(
                    view, src, (*sleAmm)[sfAsset], (*sleAmm)[sfAsset2]))
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

inline TER
checkRequireAuth(
    ReadView const& view,
    Issue const& issue,
    AccountID const& acct)
{
    auto const sleAssetIssuer = view.read(keylet::account(issue.account));
    if (!sleAssetIssuer)
        return tecINTERNAL;

    auto const sleLine =
        view.read(keylet::line(issue.account, acct, issue.currency));
    if (!sleLine)
        return terNO_LINE;

    if (sleAssetIssuer->isFlag(lsfRequireAuth))
    {
        auto const authFlag = issue.account > acct ? lsfHighAuth : lsfLowAuth;

        if (!sleLine->isFlag(authFlag))
            return terNO_LINE;
    }
    return tesSUCCESS;
}

inline TER
checkLPTokenAuthorization(
    ReadView const& view,
    AccountID const& src,
    AccountID const& dst,
    Currency const& currency)
{
    if (!view.rules().enabled(fixLPTokenTransfer))
        return tesSUCCESS;

    if (auto const sleDst = view.read(keylet::account(dst));
        sleDst && sleDst->isFieldPresent(sfAMMID))
    {
        auto const sleAmm = view.read(keylet::amm((*sleDst)[sfAMMID]));
        if (!sleAmm)
            return tecINTERNAL;

        if (auto const ter = checkRequireAuth(view, (*sleAmm)[sfAsset], src);
            ter != tesSUCCESS)
            return ter;

        if (auto const ter = checkRequireAuth(view, (*sleAmm)[sfAsset2], src);
            ter != tesSUCCESS)
            return ter;
    }
    else if (auto const sleSrc = view.read(keylet::account(src));
             sleSrc && sleSrc->isFieldPresent(sfAMMID))
    {
        auto const sleAmm = view.read(keylet::amm((*sleSrc)[sfAMMID]));
        if (!sleAmm)
            return tecINTERNAL;

        if (auto const ter = checkRequireAuth(view, (*sleAmm)[sfAsset], dst);
            ter != tesSUCCESS)
            return ter;

        if (auto const ter = checkRequireAuth(view, (*sleAmm)[sfAsset2], dst);
            ter != tesSUCCESS)
            return ter;
    }

    return tesSUCCESS;
}

}  // namespace ripple

#endif
