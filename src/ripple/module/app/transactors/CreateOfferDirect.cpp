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
#include <beast/streams/debug_ostream.h>

namespace ripple {

/** Fill offer as much as possible by consuming offers already on the books.
    We adjusts account balances and charges fees on top to taker.

    @param taker_amount.in How much the taker offers
    @param taker_amount.out How much the taker wants
    @param taker_flow.in    What the taker actually paid, not including fees.
    @param taker_flow.out   What the taker actually got, not including fees.

    @return tesSUCCESS, terNO_ACCOUNT, telFAILED_PROCESSING, or
            tecFAILED_PROCESSING
*/
std::pair<TER, core::Amounts>
CreateOfferDirect::crossOffers (
    core::LedgerView& view,
    core::Amounts const& taker_amount)
{
    core::Taker::Options const options (mTxn.getFlags());

    core::Clock::time_point const when (
        mEngine->getLedger ()->getParentCloseTimeNC ());

    core::LedgerView view_cancel (view.duplicate());
    core::OfferStream offers (view, view_cancel, 
        core::Book (
            core::AssetRef (
                taker_amount.in.getCurrency(), taker_amount.in.getIssuer()), 
            core::AssetRef (
                taker_amount.out.getCurrency(), taker_amount.out.getIssuer())), 
        when, m_journal);
    core::Taker taker (offers.view(), mTxnAccountID, taker_amount, options);

    TER cross_result (tesSUCCESS);

    while (true)
    {
        // Modifying the order or logic of these
        // operations causes a protocol breaking change.

        // Checks which remove offers are performed early so we
        // can reduce the size of the order book as much as possible
        // before terminating the loop.

        if (taker.done())
        {
            m_journal.debug << "The taker reports he's done during crossing!";
            break;
        }

        // NIKB CHECKME Investigate whether we can use offer.step_account() here
        //              or whether doing so would cause a protocol-breaking
        //             change.
        if (! offers.step ())
        {
            // Place the order since there are no
            // more offers and the order has a balance.
            m_journal.debug << "No more offers to consider during crossing!";
            break;
        }

        auto const& offer (offers.tip());

        if (taker.reject (offer.quality()))
        {
            // Place the order since there are no more offers
            // at the desired quality, and the order has a balance.
            break;
        }

        if (offer.account() == taker.account())
        {
            // Skip offer from self. The offer will be considered expired and
            // will get deleted.
            continue;
        }

        if (m_journal.debug) m_journal.debug <<
                "  Offer: " << offer.entry()->getIndex() << std::endl <<
                "         " << offer.amount().in << " : " << offer.amount().out;

        cross_result = taker.cross (offer);

        if (cross_result != tesSUCCESS)
        {
            cross_result = tecFAILED_PROCESSING;
            break;
        }
    }

    return std::make_pair(cross_result, taker.remaining_offer ());
}

}
