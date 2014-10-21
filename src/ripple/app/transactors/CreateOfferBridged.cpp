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

namespace ripple {

std::pair<bool, core::Quality>
CreateOffer::select_path (
    bool have_direct, core::OfferStream const& direct,
    bool have_bridge, core::OfferStream const& leg1, core::OfferStream const& leg2)
{
    // If we don't have any viable path, why are we here?!
    assert (have_direct || have_bridge);

    // If there's no bridged path, the direct is the best by default.
    if (!have_bridge)
        return std::make_pair (true, direct.tip ().quality ());

    core::Quality const bridged_quality (core::composed_quality (
        leg1.tip ().quality (), leg2.tip ().quality ()));

    if (have_direct)
    {
        // We compare the quality of the composed quality of the bridged offers
        // and compare it against the direct offer to pick the best.
        core::Quality const direct_quality (direct.tip ().quality ());

        if (bridged_quality < direct_quality)
            return std::make_pair (true, direct_quality);
    }

    // Either there was no direct offer, or it didn't have a better quality than
    // the bridge.
    return std::make_pair (false, bridged_quality);
}

std::pair<TER, core::Amounts>
CreateOffer::bridged_cross (
    core::Taker& taker,
    core::LedgerView& view,
    core::LedgerView& view_cancel,
    core::Clock::time_point const when)
{
    auto const& taker_amount = taker.original_offer ();

    assert (!isXRP (taker_amount.in)&& !isXRP (taker_amount.in));

    if (isXRP (taker_amount.in) || isXRP (taker_amount.out))
        throw std::logic_error ("Bridging with XRP and an endpoint.");

    core::OfferStream offers_direct (view, view_cancel,
        Book (taker.issue_in (), taker.issue_out ()), when, m_journal);

    core::OfferStream offers_leg1 (view, view_cancel,
        Book (taker.issue_in (), xrpIssue ()), when, m_journal);

    core::OfferStream offers_leg2 (view, view_cancel,
        Book (xrpIssue (), taker.issue_out ()), when, m_journal);

    TER cross_result = tesSUCCESS;

    // Note the subtle distinction here: self-offers encountered in the bridge
    // are taken, but self-offers encountered in the direct book are not.
    bool have_bridge = offers_leg1.step () && offers_leg2.step ();
    bool have_direct = step_account (offers_direct, taker);
    int count = 0;

    // Modifying the order or logic of the operations in the loop will cause
    // a protocol breaking change.
    while (have_direct || have_bridge)
    {
        bool leg1_consumed = false;
        bool leg2_consumed = false;
        bool direct_consumed = false;

        core::Quality quality;
        bool use_direct;

        std::tie (use_direct, quality) = select_path (
            have_direct, offers_direct,
            have_bridge, offers_leg1, offers_leg2);

        // We are always looking at the best quality; we are done with crossing
        // as soon as we cross the quality boundary.
        if (taker.reject(quality))
            break;

        count++;

        if (use_direct)
        {
            if (m_journal.debug)
            {
                m_journal.debug << count << " Direct:";
                m_journal.debug << "  offer: " << offers_direct.tip ();
                m_journal.debug << "     in: " << offers_direct.tip ().amount().in;
                m_journal.debug << "    out: " << offers_direct.tip ().amount ().out;
                m_journal.debug << "  owner: " << offers_direct.tip ().owner ();
                m_journal.debug << "  funds: " << view.accountFunds (
                    offers_direct.tip ().owner (),
                    offers_direct.tip ().amount ().out,
                    fhIGNORE_FREEZE);
            }

            cross_result = taker.cross(offers_direct.tip ());

            m_journal.debug << "Direct Result: " << transToken (cross_result);

            if (dry_offer (view, offers_direct.tip ()))
            {
                direct_consumed = true;
                have_direct = step_account (offers_direct, taker);
            }
        }
        else
        {
            if (m_journal.debug)
            {
                auto const owner1_funds_before = view.accountFunds (
                    offers_leg1.tip ().owner (),
                    offers_leg1.tip ().amount ().out,
                    fhIGNORE_FREEZE);

                auto const owner2_funds_before = view.accountFunds (
                    offers_leg2.tip ().owner (),
                    offers_leg2.tip ().amount ().out,
                    fhIGNORE_FREEZE);

                m_journal.debug << count << " Bridge:";
                m_journal.debug << " offer1: " << offers_leg1.tip ();
                m_journal.debug << "     in: " << offers_leg1.tip ().amount().in;
                m_journal.debug << "    out: " << offers_leg1.tip ().amount ().out;
                m_journal.debug << "  owner: " << offers_leg1.tip ().owner ();
                m_journal.debug << "  funds: " << owner1_funds_before;
                m_journal.debug << " offer2: " << offers_leg2.tip ();
                m_journal.debug << "     in: " << offers_leg2.tip ().amount ().in;
                m_journal.debug << "    out: " << offers_leg2.tip ().amount ().out;
                m_journal.debug << "  owner: " << offers_leg2.tip ().owner ();
                m_journal.debug << "  funds: " << owner2_funds_before;
            }

            cross_result = taker.cross(offers_leg1.tip (), offers_leg2.tip ());

            m_journal.debug << "Bridge Result: " << transToken (cross_result);

            if (dry_offer (view, offers_leg1.tip ()))
            {
                leg1_consumed = true;
                have_bridge = (have_bridge && offers_leg1.step ());
            }
            if (dry_offer (view, offers_leg2.tip ()))
            {
                leg2_consumed = true;
                have_bridge = (have_bridge && offers_leg2.step ());
            }
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

        // Postcondition: If we aren't done, then we *must* have consumed at
        //                least one offer fully.
        assert (direct_consumed || leg1_consumed || leg2_consumed);

        if (!direct_consumed && !leg1_consumed && !leg2_consumed)
            throw std::logic_error ("bridged crossing: nothing was fully consumed.");
    }

    return std::make_pair(cross_result, taker.remaining_offer ());
}

}
