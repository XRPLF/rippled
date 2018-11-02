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

#include <ripple/app/tx/impl/CreateCheck.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

NotTEC
CreateCheck::preflight (PreflightContext const& ctx)
{
    if (! ctx.rules.enabled (featureChecks))
        return temDISABLED;

    NotTEC const ret {preflight1 (ctx)};
    if (! isTesSuccess (ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        // There are no flags (other than universal) for CreateCheck yet.
        JLOG(ctx.j.warn()) << "Malformed transaction: Invalid flags set.";
        return temINVALID_FLAG;
    }
    if (ctx.tx[sfAccount] == ctx.tx[sfDestination])
    {
        // They wrote a check to themselves.
        JLOG(ctx.j.warn()) << "Malformed transaction: Check to self.";
        return temREDUNDANT;
    }

    {
        STAmount const sendMax {ctx.tx.getFieldAmount (sfSendMax)};
        if (!isLegalNet (sendMax) || sendMax.signum() <= 0)
        {
            JLOG(ctx.j.warn()) << "Malformed transaction: bad sendMax amount: "
                << sendMax.getFullText();
            return temBAD_AMOUNT;
        }

        if (badCurrency() == sendMax.getCurrency())
        {
            JLOG(ctx.j.warn()) <<"Malformed transaction: Bad currency.";
            return temBAD_CURRENCY;
        }
    }

    if (auto const optExpiry = ctx.tx[~sfExpiration])
    {
        if (*optExpiry == 0)
        {
            JLOG(ctx.j.warn()) << "Malformed transaction: bad expiration";
            return temBAD_EXPIRATION;
        }
    }

    return preflight2 (ctx);
}

TER
CreateCheck::preclaim (PreclaimContext const& ctx)
{
    AccountID const dstId {ctx.tx[sfDestination]};
    auto const sleDst = ctx.view.read (keylet::account (dstId));
    if (! sleDst)
    {
        JLOG(ctx.j.warn()) << "Destination account does not exist.";
        return tecNO_DST;
    }

    if ((sleDst->getFlags() & lsfRequireDestTag) &&
        !ctx.tx.isFieldPresent (sfDestinationTag))
    {
        // The tag is basically account-specific information we don't
        // understand, but we can require someone to fill it in.
        JLOG(ctx.j.warn()) << "Malformed transaction: DestinationTag required.";
        return tecDST_TAG_NEEDED;
    }

    {
        STAmount const sendMax {ctx.tx[sfSendMax]};
        if (! sendMax.native())
        {
            // The currency may not be globally frozen
            AccountID const& issuerId {sendMax.getIssuer()};
            if (isGlobalFrozen (ctx.view, issuerId))
            {
                JLOG(ctx.j.warn()) << "Creating a check for frozen asset";
                return tecFROZEN;
            }
            // If this account has a trustline for the currency, that
            // trustline may not be frozen.
            //
            // Note that we DO allow create check for a currency that the
            // account does not yet have a trustline to.
            AccountID const srcId {ctx.tx.getAccountID (sfAccount)};
            if (issuerId != srcId)
            {
                // Check if the issuer froze the line
                auto const sleTrust = ctx.view.read (
                    keylet::line (srcId, issuerId, sendMax.getCurrency()));
                if (sleTrust &&
                    sleTrust->isFlag (
                        (issuerId > srcId) ? lsfHighFreeze : lsfLowFreeze))
                {
                    JLOG(ctx.j.warn())
                        << "Creating a check for frozen trustline.";
                    return tecFROZEN;
                }
            }
            if (issuerId != dstId)
            {
                // Check if dst froze the line.
                auto const sleTrust = ctx.view.read (
                    keylet::line (issuerId, dstId, sendMax.getCurrency()));
                if (sleTrust &&
                    sleTrust->isFlag (
                        (dstId > issuerId) ? lsfHighFreeze : lsfLowFreeze))
                {
                    JLOG(ctx.j.warn())
                        << "Creating a check for destination frozen trustline.";
                    return tecFROZEN;
                }
            }
        }
    }
    {
        using duration = NetClock::duration;
        using timepoint = NetClock::time_point;
        auto const optExpiry = ctx.tx[~sfExpiration];

        // Expiration is defined in terms of the close time of the parent
        // ledger, because we definitively know the time that it closed but
        // we do not know the closing time of the ledger that is under
        // construction.
        if (optExpiry &&
            (ctx.view.parentCloseTime() >= timepoint {duration {*optExpiry}}))
        {
            JLOG(ctx.j.warn()) << "Creating a check that has already expired.";
            return tecEXPIRED;
        }
    }
    return tesSUCCESS;
}

TER
CreateCheck::doApply ()
{
    auto const sle = view().peek (keylet::account (account_));
    if (! sle)
        return tefINTERNAL;

    // A check counts against the reserve of the issuing account, but we
    // check the starting balance because we want to allow dipping into the
    // reserve to pay fees.
    {
        STAmount const reserve {view().fees().accountReserve (
            sle->getFieldU32 (sfOwnerCount) + 1)};

        if (mPriorBalance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    AccountID const dstAccountId {ctx_.tx[sfDestination]};
    std::uint32_t const seq {ctx_.tx.getSequence()};
    auto sleCheck =
        std::make_shared<SLE>(ltCHECK, getCheckIndex (account_, seq));

    sleCheck->setAccountID (sfAccount, account_);
    sleCheck->setAccountID (sfDestination, dstAccountId);
    sleCheck->setFieldU32 (sfSequence, seq);
    sleCheck->setFieldAmount (sfSendMax, ctx_.tx[sfSendMax]);
    if (auto const srcTag = ctx_.tx[~sfSourceTag])
        sleCheck->setFieldU32 (sfSourceTag, *srcTag);
    if (auto const dstTag = ctx_.tx[~sfDestinationTag])
        sleCheck->setFieldU32 (sfDestinationTag, *dstTag);
    if (auto const invoiceId = ctx_.tx[~sfInvoiceID])
        sleCheck->setFieldH256 (sfInvoiceID, *invoiceId);
    if (auto const expiry = ctx_.tx[~sfExpiration])
        sleCheck->setFieldU32 (sfExpiration, *expiry);

    view().insert (sleCheck);

    auto viewJ = ctx_.app.journal ("View");
    // If it's not a self-send (and it shouldn't be), add Check to the
    // destination's owner directory.
    if (dstAccountId != account_)
    {
        auto const page = dirAdd (view(), keylet::ownerDir (dstAccountId),
            sleCheck->key(), false, describeOwnerDir (dstAccountId), viewJ);

        JLOG(j_.trace())
            << "Adding Check to destination directory "
            << to_string (sleCheck->key())
            << ": " << (page ? "success" : "failure");

        if (! page)
            return tecDIR_FULL;

        sleCheck->setFieldU64 (sfDestinationNode, *page);
    }

    {
        auto const page = dirAdd (view(), keylet::ownerDir (account_),
            sleCheck->key(), false, describeOwnerDir (account_), viewJ);

        JLOG(j_.trace())
            << "Adding Check to owner directory "
            << to_string (sleCheck->key())
            << ": " << (page ? "success" : "failure");

        if (! page)
            return tecDIR_FULL;

        sleCheck->setFieldU64 (sfOwnerNode, *page);
    }
    // If we succeeded, the new entry counts against the creator's reserve.
    adjustOwnerCount (view(), sle, 1, viewJ);
    return tesSUCCESS;
}

} // namespace ripple
