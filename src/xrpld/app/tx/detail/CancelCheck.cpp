//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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

#include <ripple/app/tx/impl/CancelCheck.h>

#include <ripple/app/ledger/Ledger.h>
#include <ripple/basics/Log.h>
#include <ripple/ledger/ApplyView.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

NotTEC
CancelCheck::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureChecks))
        return temDISABLED;

    NotTEC const ret{preflight1(ctx)};
    if (!isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        // There are no flags (other than universal) for CreateCheck yet.
        JLOG(ctx.j.warn()) << "Malformed transaction: Invalid flags set.";
        return temINVALID_FLAG;
    }

    return preflight2(ctx);
}

TER
CancelCheck::preclaim(PreclaimContext const& ctx)
{
    auto const sleCheck = ctx.view.read(keylet::check(ctx.tx[sfCheckID]));
    if (!sleCheck)
    {
        JLOG(ctx.j.warn()) << "Check does not exist.";
        return tecNO_ENTRY;
    }

    using duration = NetClock::duration;
    using timepoint = NetClock::time_point;
    auto const optExpiry = (*sleCheck)[~sfExpiration];

    // Expiration is defined in terms of the close time of the parent
    // ledger, because we definitively know the time that it closed but
    // we do not know the closing time of the ledger that is under
    // construction.
    if (!optExpiry ||
        (ctx.view.parentCloseTime() < timepoint{duration{*optExpiry}}))
    {
        // If the check is not yet expired, then only the creator or the
        // destination may cancel the check.
        AccountID const acctId{ctx.tx[sfAccount]};
        if (acctId != (*sleCheck)[sfAccount] &&
            acctId != (*sleCheck)[sfDestination])
        {
            JLOG(ctx.j.warn()) << "Check is not expired and canceler is "
                                  "neither check source nor destination.";
            return tecNO_PERMISSION;
        }
    }
    return tesSUCCESS;
}

TER
CancelCheck::doApply()
{
    auto const sleCheck = view().peek(keylet::check(ctx_.tx[sfCheckID]));
    if (!sleCheck)
    {
        // Error should have been caught in preclaim.
        JLOG(j_.warn()) << "Check does not exist.";
        return tecNO_ENTRY;
    }

    AccountID const srcId{sleCheck->getAccountID(sfAccount)};
    AccountID const dstId{sleCheck->getAccountID(sfDestination)};
    auto viewJ = ctx_.app.journal("View");

    // If the check is not written to self (and it shouldn't be), remove the
    // check from the destination account root.
    if (srcId != dstId)
    {
        std::uint64_t const page{(*sleCheck)[sfDestinationNode]};
        if (!view().dirRemove(
                keylet::ownerDir(dstId), page, sleCheck->key(), true))
        {
            JLOG(j_.fatal()) << "Unable to delete check from destination.";
            return tefBAD_LEDGER;
        }
    }
    {
        std::uint64_t const page{(*sleCheck)[sfOwnerNode]};
        if (!view().dirRemove(
                keylet::ownerDir(srcId), page, sleCheck->key(), true))
        {
            JLOG(j_.fatal()) << "Unable to delete check from owner.";
            return tefBAD_LEDGER;
        }
    }

    // If we succeeded, update the check owner's reserve.
    auto const sleSrc = view().peek(keylet::account(srcId));
    adjustOwnerCount(view(), sleSrc, -1, viewJ);

    // Remove check from ledger.
    view().erase(sleCheck);
    return tesSUCCESS;
}

}  // namespace ripple
