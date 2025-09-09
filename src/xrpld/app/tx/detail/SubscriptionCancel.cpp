//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/paths/Flow.h>
#include <xrpld/app/tx/detail/SubscriptionCancel.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/scope.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
SubscriptionCancel::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSubscription))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    return preflight2(ctx);
}

TER
SubscriptionCancel::preclaim(PreclaimContext const& ctx)
{
    auto const sleSub = ctx.view.read(
        keylet::subscription(ctx.tx.getFieldH256(sfSubscriptionID)));
    if (!sleSub)
    {
        JLOG(ctx.j.debug())
            << "SubscriptionCancel: Subscription does not exist.";
        return tecNO_ENTRY;
    }

    return tesSUCCESS;
}

TER
SubscriptionCancel::doApply()
{
    Sandbox sb(&ctx_.view());

    auto const sleSub =
        sb.peek(keylet::subscription(ctx_.tx.getFieldH256(sfSubscriptionID)));
    if (!sleSub)
    {
        JLOG(ctx_.journal.debug())
            << "SubscriptionCancel: Subscription does not exist.";
        return tecINTERNAL;
    }

    AccountID const account{sleSub->getAccountID(sfAccount)};
    AccountID const dstAcct{sleSub->getAccountID(sfDestination)};
    auto viewJ = ctx_.app.journal("View");

    std::uint64_t const ownerPage{(*sleSub)[sfOwnerNode]};
    if (!sb.dirRemove(
            keylet::ownerDir(account), ownerPage, sleSub->key(), true))
    {
        JLOG(j_.fatal()) << "Unable to delete subscription from source.";
        return tefBAD_LEDGER;
    }

    std::uint64_t const destPage{(*sleSub)[sfDestinationNode]};
    if (!sb.dirRemove(keylet::ownerDir(dstAcct), destPage, sleSub->key(), true))
    {
        JLOG(j_.fatal()) << "Unable to delete subscription from destination.";
        return tefBAD_LEDGER;
    }

    auto const sleSrc = sb.peek(keylet::account(account));
    sb.erase(sleSub);

    adjustOwnerCount(sb, sleSrc, -1, viewJ);

    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

}  // namespace ripple
