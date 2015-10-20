//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/app/tx/impl/CreateTicket.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Indexes.h>

namespace ripple {

TER
CreateTicket::preflight (PreflightContext const& ctx)
{
#if ! RIPPLE_ENABLE_TICKETS
    if (! (ctx.flags & tapENABLE_TESTING))
        return temDISABLED;
#endif

    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;

    if (ctx.tx.isFieldPresent (sfExpiration))
    {
        if (ctx.tx.getFieldU32 (sfExpiration) == 0)
        {
            JLOG(ctx.j.warning) <<
                "Malformed transaction: bad expiration";
            return temBAD_EXPIRATION;
        }
    }

    return preflight2 (ctx);
}

TER
CreateTicket::doApply ()
{
    auto const sle = view().peek(keylet::account(account_));

    // A ticket counts against the reserve of the issuing account, but we
    // check the starting balance because we want to allow dipping into the
    // reserve to pay fees.
    {
        auto const reserve = view().fees().accountReserve(
            sle->getFieldU32(sfOwnerCount) + 1);
        
        if (mPriorBalance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    std::uint32_t expiration (0);

    if (ctx_.tx.isFieldPresent (sfExpiration))
    {
        expiration = ctx_.tx.getFieldU32 (sfExpiration);

        if (view().parentCloseTime() >= expiration)
            return tesSUCCESS;
    }

    SLE::pointer sleTicket = std::make_shared<SLE>(ltTICKET,
        getTicketIndex (account_, ctx_.tx.getSequence ()));
    sleTicket->setAccountID (sfAccount, account_);
    sleTicket->setFieldU32 (sfSequence, ctx_.tx.getSequence ());
    if (expiration != 0)
        sleTicket->setFieldU32 (sfExpiration, expiration);
    view().insert (sleTicket);

    if (ctx_.tx.isFieldPresent (sfTarget))
    {
        AccountID const target_account (ctx_.tx.getAccountID (sfTarget));

        SLE::pointer sleTarget = view().peek (keylet::account(target_account));

        // Destination account does not exist.
        if (!sleTarget)
            return tecNO_TARGET;

        // The issuing account is the default account to which the ticket
        // applies so don't bother saving it if that's what's specified.
        if (target_account != account_)
            sleTicket->setAccountID (sfTarget, target_account);
    }

    std::uint64_t hint;

    auto describer = [&](SLE::pointer p, bool b)
    {
        ownerDirDescriber(p, b, account_);
    };

    auto viewJ = ctx_.app.journal ("View");
    TER result = dirAdd(view(),
        hint,
        getOwnerDirIndex (account_),
        sleTicket->getIndex (),
        describer,
        viewJ);

    if (j_.trace) j_.trace <<
        "Creating ticket " << to_string (sleTicket->getIndex ()) <<
        ": " << transHuman (result);

    if (result != tesSUCCESS)
        return result;

    sleTicket->setFieldU64(sfOwnerNode, hint);

    // If we succeeded, the new entry counts agains the creator's reserve.
    adjustOwnerCount(view(), sle, 1, viewJ);

    return result;
}

}
