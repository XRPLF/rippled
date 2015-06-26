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
#include <ripple/app/misc/CanonicalTXSet.h>

namespace ripple {

bool operator< (CanonicalTXSet::Key const& lhs, CanonicalTXSet::Key const& rhs)
{
    if (lhs.account_ < rhs.account_) return true;

    if (lhs.account_ > rhs.account_) return false;

    // We want to sort transactions without Tickets in front of transactions
    // with Tickets.  This encourages transactions that create and cancel
    // Tickets to precede transactions that consume Tickets.  Therefore we
    // decrement the sequence numbers (so zero -> max<std::uint32_t>) before
    // comparing.  Underflow is well defined since seq is unsigned.
    {
        std::uint32_t const lhsSeq = lhs.seq_ - 1;
        std::uint32_t const rhsSeq = rhs.seq_ - 1;

        if (lhsSeq < rhsSeq) return true;

        if (lhsSeq > rhsSeq) return false;
    }

    // Comparisons to make if both Keys are Tickets (inferred by seq == 0).
    if ((lhs.seq_ | rhs.seq_) == 0)
    {
        if (lhs.ticketOwner_ < rhs.ticketOwner_) return true;

        if (lhs.ticketOwner_ > rhs.ticketOwner_) return false;

        if (lhs.ticketSeq_ < rhs.ticketSeq_) return true;

        if (lhs.ticketSeq_ > rhs.ticketSeq_) return false;
    }
    return lhs.txId_ < rhs.txId_;
}

void CanonicalTXSet::push_back (STTx::ref txn)
{
    uint256 effectiveAccount = mSetHash;

    effectiveAccount ^= to256 (txn->getAccountID(sfAccount));

    // See if we have a TicketID to deal with.
    AccountID ticketOwner {0};
    std::uint32_t ticketSeq = 0;
    std::uint32_t const seq = txn->getSequence ();
    if ((seq == 0) && (txn->isFieldPresent (sfTicketID)))
    {
        auto const& ticketID = txn->getFieldObject (sfTicketID);
        ticketOwner = ticketID.getAccountID (sfAccount);
        ticketSeq = ticketID.getFieldU32 (sfSequence);
    }

    mMap.emplace (std::piecewise_construct,
        std::make_tuple (effectiveAccount, seq,
            ticketOwner, ticketSeq, txn->getTransactionID ()),
        std::make_tuple (txn));
}

CanonicalTXSet::iterator CanonicalTXSet::erase (iterator const& it)
{
    iterator tmp = it;
    ++tmp;
    mMap.erase (it);
    return tmp;
}

} // ripple
