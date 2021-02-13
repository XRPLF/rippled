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

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/tx/impl/CreateTicket.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/st.h>

namespace ripple {

TxConsequences
CreateTicket::makeTxConsequences(PreflightContext const& ctx)
{
    // Create TxConsequences identifying the number of sequences consumed.
    return TxConsequences{ctx.tx, ctx.tx[sfTicketCount]};
}

NotTEC
CreateTicket::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureTicketBatch))
        return temDISABLED;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    if (std::uint32_t const count = ctx.tx[sfTicketCount];
        count < minValidCount || count > maxValidCount)
        return temINVALID_COUNT;

    if (NotTEC const ret{preflight1(ctx)}; !isTesSuccess(ret))
        return ret;

    return preflight2(ctx);
}

TER
CreateTicket::preclaim(PreclaimContext const& ctx)
{
    auto const id = ctx.tx[sfAccount];
    auto const sleAccountRoot = ctx.view.read(keylet::account(id));
    if (!sleAccountRoot)
        return terNO_ACCOUNT;

    // Make sure the TicketCreate would not cause the account to own
    // too many tickets.
    std::uint32_t const curTicketCount =
        (*sleAccountRoot)[~sfTicketCount].value_or(0u);
    std::uint32_t const addedTickets = ctx.tx[sfTicketCount];
    std::uint32_t const consumedTickets =
        ctx.tx.getSeqProxy().isTicket() ? 1u : 0u;

    // Note that unsigned integer underflow can't currently happen because
    //  o curTicketCount   >= 0
    //  o addedTickets     >= 1
    //  o consumedTickets  <= 1
    // So in the worst case addedTickets == consumedTickets and the
    // computation yields curTicketCount.
    if (curTicketCount + addedTickets - consumedTickets > maxTicketThreshold)
        return tecDIR_FULL;

    return tesSUCCESS;
}

TER
CreateTicket::doApply()
{
    SLE::pointer const sleAccountRoot = view().peek(keylet::account(account_));
    if (!sleAccountRoot)
        return tefINTERNAL;

    // Each ticket counts against the reserve of the issuing account, but we
    // check the starting balance because we want to allow dipping into the
    // reserve to pay fees.
    std::uint32_t const ticketCount = ctx_.tx[sfTicketCount];
    {
        XRPAmount const reserve = view().fees().accountReserve(
            sleAccountRoot->getFieldU32(sfOwnerCount) + ticketCount);

        if (mPriorBalance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    beast::Journal viewJ{ctx_.app.journal("View")};

    // The starting ticket sequence is the same as the current account
    // root sequence.  Before we got here to doApply(), the transaction
    // machinery already incremented the account root sequence if that
    // was appropriate.
    std::uint32_t const firstTicketSeq = (*sleAccountRoot)[sfSequence];

    // Sanity check that the transaction machinery really did already
    // increment the account root Sequence.
    if (std::uint32_t const txSeq = ctx_.tx[sfSequence];
        txSeq != 0 && txSeq != (firstTicketSeq - 1))
        return tefINTERNAL;

    for (std::uint32_t i = 0; i < ticketCount; ++i)
    {
        std::uint32_t const curTicketSeq = firstTicketSeq + i;

        SLE::pointer sleTicket = std::make_shared<SLE>(
            ltTICKET, getTicketIndex(account_, curTicketSeq));

        sleTicket->setAccountID(sfAccount, account_);
        sleTicket->setFieldU32(sfTicketSequence, curTicketSeq);
        view().insert(sleTicket);

        auto const page = view().dirInsert(
            keylet::ownerDir(account_),
            sleTicket->key(),
            describeOwnerDir(account_));

        JLOG(j_.trace()) << "Creating ticket " << to_string(sleTicket->key())
                         << ": " << (page ? "success" : "failure");

        if (!page)
            return tecDIR_FULL;

        sleTicket->setFieldU64(sfOwnerNode, *page);
    }

    // Update the record of the number of Tickets this account owns.
    std::uint32_t const oldTicketCount =
        (*(sleAccountRoot))[~sfTicketCount].value_or(0u);

    sleAccountRoot->setFieldU32(sfTicketCount, oldTicketCount + ticketCount);

    // Every added Ticket counts against the creator's reserve.
    adjustOwnerCount(view(), sleAccountRoot, ticketCount, viewJ);

    // TicketCreate is the only transaction that can cause an account root's
    // Sequence field to increase by more than one.  October 2018.
    sleAccountRoot->setFieldU32(sfSequence, firstTicketSeq + ticketCount);

    return tesSUCCESS;
}

}  // namespace ripple
