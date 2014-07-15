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

namespace ripple {

TER CancelTicket::doApply ()
{
    assert (mTxnAccount);

    uint256 const ticketId = mTxn.getFieldH256 (sfTicketID);

    SLE::pointer sleTicket = mEngine->view ().entryCache (ltTICKET, ticketId);

    if (!sleTicket)
        return tecNO_ENTRY;

    Account const ticket_owner (sleTicket->getFieldAccount160 (sfAccount));
    
    bool authorized (mTxnAccountID == ticket_owner);

    // The target can also always remove a ticket
    if (!authorized && sleTicket->isFieldPresent (sfTarget))
        authorized = (mTxnAccountID == sleTicket->getFieldAccount160 (sfTarget));

    // And finally, anyone can remove an expired ticket
    if (!authorized && sleTicket->isFieldPresent (sfExpiration))
    {
        std::uint32_t const expiration = sleTicket->getFieldU32 (sfExpiration);

        if (mEngine->getLedger ()->getParentCloseTimeNC () >= expiration)
            authorized = true;
    }

    if (!authorized)
        return tecNO_PERMISSION;

    std::uint64_t const hint (sleTicket->getFieldU64 (sfOwnerNode));

    TER const result = mEngine->view ().dirDelete (false, hint,
        Ledger::getOwnerDirIndex (ticket_owner), ticketId, false, (hint == 0));

    if (result == tesSUCCESS)
    {
        mEngine->view ().ownerCountAdjust (ticket_owner, -1);
        mEngine->view ().entryDelete (sleTicket);
    }

    return result;
}

}
