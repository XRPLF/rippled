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

#include <ripple/module/app/book/OfferStream.h>
#include <ripple/module/app/book/Taker.h>
#include <ripple/module/app/book/Quality.h>
#include <beast/streams/debug_ostream.h>

namespace ripple {

std::pair<TER, core::Amounts>
CreateOfferBridged::crossOffers (
    core::LedgerView& view,
    core::Amounts const& taker_amount)
{
    assert (!taker_amount.in.isNative () && !taker_amount.out.isNative ());

    if (taker_amount.in.isNative () || taker_amount.out.isNative ())
        return std::make_pair (tefINTERNAL, core::Amounts ());

    core::Clock::time_point const when (
        mEngine->getLedger ()->getParentCloseTimeNC ());

    core::Taker::Options const options (mTxn.getFlags());

    core::LedgerView view_cancel (view.duplicate());

    core::AssetRef const asset_in (
        taker_amount.in.getCurrency(), taker_amount.in.getIssuer());

    core::AssetRef const asset_out (
        taker_amount.out.getCurrency(), taker_amount.out.getIssuer());

    core::OfferStream offers_direct (view, view_cancel,
        core::Book (asset_in, asset_out), when, m_journal);

    core::OfferStream offers_leg1 (view, view_cancel,
        core::Book (asset_in, xrp_asset ()), when, m_journal);
    
    core::OfferStream offers_leg2 (view, view_cancel,
        core::Book (xrp_asset (), asset_out), when, m_journal);

    core::Taker taker (view, mTxnAccountID, taker_amount, options);

    if (m_journal.debug) m_journal.debug <<
        "process_order: " <<
        (options.sell? "sell" : "buy") << " " <<
        (options.passive? "passive" : "") << std::endl <<
        "     taker: " << taker.account() << std::endl <<
        "  balances: " <<
            view.accountFunds (taker.account(), taker_amount.in) << ", " <<
            view.accountFunds (taker.account(), taker_amount.out);

    TER cross_result (tesSUCCESS);

    bool have_bridged (
        offers_leg1.step_account (taker.account()) &&
        offers_leg2.step_account (taker.account()));
    bool have_direct (offers_direct.step_account (taker.account()));
    bool place_order (true);

    while (have_direct || have_bridged)
    {
        core::Quality quality;
        bool use_direct;
        bool leg1_consumed(false);
        bool leg2_consumed(false);
        bool direct_consumed(false);

        // Logic:
        // We calculate the qualities of any direct and bridged offers at the
        // tip of the order book, and choose the best one of the two.
        
        if (have_direct)
        {
            core::Quality const direct_quality (offers_direct.tip ().quality ());

            if (have_bridged)
            {
                core::Quality const bridged_quality (core::composed_quality (
                    offers_leg1.tip ().quality (),
                    offers_leg2.tip ().quality ()));

                if (bridged_quality < direct_quality)
                {
                    use_direct = true;
                    quality = direct_quality;
                }
                else
                {
                    use_direct = false;
                    quality = bridged_quality;
                }
            }
            else
            {
                use_direct = true;
                quality = offers_direct.tip ().quality ();
            }
        }
        else
        {
            use_direct = false;
            quality = core::composed_quality (
                    offers_leg1.tip ().quality (),
                    offers_leg2.tip ().quality ());
        }

        // We are always looking at the best quality available, so if we reject
        // that, we know that we are done.
        if (taker.reject(quality))
            break;

        if (use_direct)
        {
            if (m_journal.debug) m_journal.debug <<
                "  Offer: " << offers_direct.tip () << std::endl <<
                "         " << offers_direct.tip ().amount().in <<
                " : " << offers_direct.tip ().amount ().out;

            cross_result = taker.cross(offers_direct.tip ());

            if (offers_direct.tip ().fully_consumed ())
            {
                direct_consumed = true;
                have_direct = offers_direct.step_account (taker.account());
            }
        }
        else
        {
            if (m_journal.debug) m_journal.debug <<
                " Offer1: " << offers_leg1.tip () << std::endl <<
                "         " << offers_leg1.tip ().amount().in <<
                " : " << offers_leg1.tip ().amount ().out << std::endl <<
                " Offer2: " << offers_leg2.tip () << std::endl <<
                "         " << offers_leg2.tip ().amount ().in <<
                " : " << offers_leg2.tip ().amount ().out;

            cross_result = taker.cross(offers_leg1.tip (), offers_leg2.tip ());

            if (offers_leg1.tip ().fully_consumed ())
            {
                leg1_consumed = true;
                have_bridged = offers_leg1.step_account (taker.account ());
            }
            if (have_bridged && offers_leg2.tip ().fully_consumed ())
            {
                leg2_consumed = true;
                have_bridged = offers_leg2.step_account (taker.account ());
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
            place_order = false;
            break;
        }

        // Postcondition: If we aren't done, then we must have consumed at
        //                least one offer fully.
        assert (direct_consumed || leg1_consumed || leg2_consumed);
    }

    return std::make_pair(cross_result, taker.remaining_offer ());
}

}

