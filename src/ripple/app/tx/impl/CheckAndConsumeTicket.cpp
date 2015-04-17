//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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
#include <ripple/app/tx/impl/CheckAndConsumeTicket.h>
#include <ripple/app/tx/TransactionEngine.h>              // TransactionEngine
#include <ripple/app/ledger/MetaView.h>                   // MetaView
#include <ripple/protocol/Indexes.h>                      // getOwnerDirIndex
#include <ripple/protocol/STTx.h>                         // STTx


namespace ripple {

namespace
{

struct TicketContents
{
    AccountID owner;
    std::uint32_t seq;

    TicketContents () = delete;
    TicketContents (AccountID owner_, std::uint32_t seq_)
    : owner (owner_)
    , seq (seq_)
    { }
};

TicketContents getTicketContents (STTx const& txn)
{
    auto const& ticketID = txn.getFieldObject (sfTicketID);

    return {ticketID.getAccountID (sfAccount),
        ticketID.getFieldU32 (sfSequence)};
}

// Return the appropriate error code for this missing Ticket.
TER missingTicketTER (TicketContents const& ticket, SLE::ref sleOwner)
{
    // If the account owner can't be found give up.
    if (! sleOwner)
        return tefNO_ENTRY;

    // 1. If the Ticket sequence exceeds the ticket owner's seq, then the
    //    Ticket may not have been created in this ledger yet.  Retry.
    //
    // 2. If the Ticket sequence is the same or less than the ticket owner's
    //    seq, then the Ticket was either never created or was consumed.  Fail.
    auto const ownerSeq = sleOwner->getFieldU32 (sfSequence);
    return ticket.seq >= ownerSeq ? terPRE_TICKET : tefNO_ENTRY;
}

bool authorizedToUseTicket (SLE::ref sleTicket,
    AccountID const& txnAccountID, AccountID const& ticketOwner)
{
    if (txnAccountID == ticketOwner)
        return true;

    // The target can also always consume a ticket.
    if (sleTicket->isFieldPresent (sfTarget))
        return (txnAccountID == sleTicket->getAccountID (sfTarget));

    return false;
}

bool expiredTicket (SLE::ref sleTicket, TransactionEngine* const txnEngine)
{
    if (sleTicket->isFieldPresent (sfExpiration))
    {
        std::uint32_t const expiration = sleTicket->getFieldU32 (sfExpiration);

        if (txnEngine->getLedger()->getParentCloseTimeNC() >= expiration)
            return true;
    }
    return false;
}

TER consumeTicket (
    SLE::ref sleTicket,
    SLE::ref sleOwner,
    AccountID const& owner,
    uint256 const& ticketIndex,
    MetaView& view)
{
    std::uint64_t const hint = sleTicket->getFieldU64 (sfOwnerNode);

    TER const result = dirDelete (view,
        false, hint, getOwnerDirIndex (owner), ticketIndex, false, (hint == 0));

    // If result != tesSUCCESS do we still want to do the adjustOwnerCount()
    // and erase()???  Seems like we're in big trouble either way.
    adjustOwnerCount (view, view.peek (keylet::account(owner)), -1);
    view.erase (sleTicket);

    return result;
}

} // anonymous namespace

// checkAndConsumeSeqTicket() only allows an authorized user to consume
// the Ticket.  This is in contrast to checkAndConsumeCancelTicket().
//
// Precondition: txn must contain a valid TicketID object.
TER checkAndConsumeSeqTicket (STTx const& txn,
    AccountID const& txnAccountID, TransactionEngine* const txnEngine)
{
    // Anyone calling this function should have verified txn has a Ticket.
    {
        auto hasTicket = txn.isFieldPresent (sfTicketID);
        assert (hasTicket);
        if (! hasTicket)
            return temINVALID;
    }
    // Get the TicketIndex so we can see if it's usable.
    MetaView& view = txnEngine->view();
    TicketContents const ticket = getTicketContents (txn);
    uint256 ticketIndex = getTicketIndex (ticket.owner, ticket.seq);
    SLE::pointer sleTicket = view.peek (keylet::ticket(ticketIndex));
    SLE::pointer sleOwner = view.peek (keylet::account(ticket.owner));

    if (!sleTicket)
        return missingTicketTER (ticket, sleOwner);

    // Only allow authorized users to consume a Ticket.
    if (! authorizedToUseTicket (sleTicket, txnAccountID, ticket.owner))
        return tefNO_PERMISSION;

    // See if the Ticket is expired.
    TER const result = expiredTicket (sleTicket, txnEngine) ?
        tecEXPIRED_TICKET : tesSUCCESS;

    // Even if the Ticket is expired consume the Ticket.
    {
        TER const terConsume =
            consumeTicket(sleTicket, sleOwner, ticket.owner, ticketIndex, view);

        // If the consume failed then something went very badly.
        if (terConsume != tesSUCCESS)
            return terConsume;
    }
    return result;
}

// checkAndConsumeCancelTicket() allows anyone to consume an expired Ticket.
// Only authorized users can consume an un-expired Ticket.  This is in
// contrast to checkAndConsumeSeqTicket().
//
// Precondition: txn must contain a valid TicketID object.
TER checkAndConsumeCancelTicket (STTx const& txn,
    AccountID const& txnAccountID, TransactionEngine* const txnEngine)
{
    // Anyone calling this function should have verified txn has a Ticket.
    {
        auto hasTicket = txn.isFieldPresent (sfTicketID);
        assert (hasTicket);
        if (! hasTicket)
            return temINVALID;
    }
    // Get the TicketIndex so we can see if it's usable.
    MetaView& view = txnEngine->view();
    TicketContents const ticket = getTicketContents (txn);
    uint256 ticketIndex = getTicketIndex (ticket.owner, ticket.seq);
    SLE::pointer sleTicket = view.peek (keylet::ticket(ticketIndex));
    SLE::pointer sleOwner = view.peek (keylet::account(ticket.owner));

    if (!sleTicket)
        return missingTicketTER (ticket, sleOwner);

    // See if the Ticket is expired.
    if (! expiredTicket (sleTicket, txnEngine))
    {
        // Only allow authorized users to consume an un-expired Ticket.
        if (! authorizedToUseTicket (sleTicket, txnAccountID, ticket.owner))
            return tefNO_PERMISSION;
    }

    // If we got here attempt to consume the Ticket.
    return consumeTicket(sleTicket, sleOwner, ticket.owner, ticketIndex, view);
}

} // ripple
