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

#ifndef RIPPLE_TX_CREATETICKET_H_INCLUDED
#define RIPPLE_TX_CREATETICKET_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Indexes.h>

namespace ripple {

class CreateTicket : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Custom};

    constexpr static std::uint32_t minValidCount = 1;

    // A note on how the maxValidCount was determined.  The goal is for
    // a single TicketCreate transaction to not use more compute power than
    // a single compute-intensive Payment.
    //
    // Timing was performed using a MacBook Pro laptop and a release build
    // with asserts off.  20 measurements were taken of each of the Payment
    // and TicketCreate transactions and averaged to get timings.
    //
    // For the example compute-intensive Payment a Discrepancy unit test
    // unit test Payment with 3 paths was chosen.  With all the latest
    // amendments enabled, that Payment::doApply() operation took, on
    // average, 1.25 ms.
    //
    // Using that same test set up creating 250 Tickets in a single
    // CreateTicket::doApply() in a unit test took, on average, 1.21 ms.
    //
    // So, for the moment, a single transaction creating 250 Tickets takes
    // about the same compute time as a single compute-intensive payment.
    //
    // October 2018.
    constexpr static std::uint32_t maxValidCount = 250;

    // The maximum number of Tickets an account may hold.  If a
    // TicketCreate would cause an account to own more than this many
    // tickets, then the TicketCreate will fail.
    //
    // The number was chosen arbitrarily and is an effort toward avoiding
    // ledger-stuffing with Tickets.
    constexpr static std::uint32_t maxTicketThreshold = 250;

    explicit CreateTicket(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    /** Enforce constraints beyond those of the Transactor base class. */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Enforce constraints beyond those of the Transactor base class. */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Precondition: fee collection is likely.  Attempt to create ticket(s). */
    TER
    doApply() override;
};

}  // namespace ripple

#endif
