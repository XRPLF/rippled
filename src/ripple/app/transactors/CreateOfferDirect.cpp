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

#include <ripple/app/book/Taker.h>
#include <stdexcept>

namespace ripple {

std::pair<TER, core::Amounts>
CreateOffer::direct_cross (
    core::Taker& taker,
    core::LedgerView& view,
    core::LedgerView& view_cancel,
    core::Clock::time_point const when)
{
    core::OfferStream offers (
        view, view_cancel,
        Book (taker.issue_in (), taker.issue_out ()),
        when, m_journal);

    TER cross_result (tesSUCCESS);
    int count = 0;

    bool have_offer = step_account (offers, taker);

    // Modifying the order or logic of the operations in the loop will cause
    // a protocol breaking change.
    while (have_offer)
    {
        bool direct_consumed = false;
        auto const& offer (offers.tip());

        // We are done with crossing as soon as we cross the quality boundary
        if (taker.reject (offer.quality()))
            break;

        count++;

        if (m_journal.debug)
        {
            m_journal.debug << count << " Direct:";
            m_journal.debug << "  offer: " << offer;
            m_journal.debug << "     in: " << offer.amount ().in;
            m_journal.debug << "    out: " << offer.amount ().out;
            m_journal.debug << "  owner: " << offer.owner ();
            m_journal.debug << "  funds: " << view.accountFunds (
                offer.owner (), offer.amount ().out, fhIGNORE_FREEZE);
        }

        cross_result = taker.cross (offer);

        m_journal.debug << "Direct Result: " << transToken (cross_result);

        if (dry_offer (view, offer))
        {
            direct_consumed = true;
            have_offer = step_account (offers, taker);
        }

        if (cross_result != tesSUCCESS)
        {
            cross_result = tecFAILED_PROCESSING;
            break;
        }

        if (taker.done())
        {
            m_journal.debug << "The taker reports he's done during crossing!";
            break;
        }

        // Postcondition: If we aren't done, then we *must* have consumed the
        //                offer on the books fully!
        assert (direct_consumed);

        if (!direct_consumed)
            throw std::logic_error ("direct crossing: nothing was fully consumed.");
    }

    return std::make_pair(cross_result, taker.remaining_offer ());
}

}
