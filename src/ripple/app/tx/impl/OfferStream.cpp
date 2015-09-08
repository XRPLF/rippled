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
#include <ripple/app/tx/impl/OfferStream.h>

namespace ripple {

OfferStream::OfferStream (ApplyView& view, ApplyView& view_cancel,
    BookRef book, Clock::time_point when, StepCounter& counter,
        Config const& config, beast::Journal journal)
    : j_ (journal)
    , m_view (view)
    , m_view_cancel (view_cancel)
    , m_book (book)
    , m_when (when)
    , m_tip (view, book)
    , config_ (config)
    , counter_ (counter)
{
}

// Handle the case where a directory item with no corresponding ledger entry
// is found. This shouldn't happen but if it does we clean it up.
void
OfferStream::erase (ApplyView& view)
{
    // NIKB NOTE This should be using ApplyView::dirDelete, which would
    //           correctly remove the directory if its the last entry.
    //           Unfortunately this is a protocol breaking change.

    auto p = view.peek (keylet::page(m_tip.dir()));

    if (p == nullptr)
    {
        if (j_.error) j_.error <<
            "Missing directory " << m_tip.dir() <<
            " for offer " << m_tip.index();
        return;
    }

    auto v (p->getFieldV256 (sfIndexes));
    auto it (std::find (v.begin(), v.end(), m_tip.index()));

    if (it == v.end())
    {
        if (j_.error) j_.error <<
            "Missing offer " << m_tip.index() <<
            " for directory " << m_tip.dir();
        return;
    }

    v.erase (it);
    p->setFieldV256 (sfIndexes, v);
    view.update (p);

    if (j_.trace) j_.trace <<
        "Missing offer " << m_tip.index() <<
        " removed from directory " << m_tip.dir();
}

bool
OfferStream::step ()
{
    // Modifying the order or logic of these
    // operations causes a protocol breaking change.

    for(;;)
    {
        // BookTip::step deletes the current offer from the view before
        // advancing to the next (unless the ledger entry is missing).
        if (! m_tip.step())
            return false;

        // If we exceed the maximum number of allowed steps, we're done.
        if (!counter_.step ())
            return false;

        std::shared_ptr<SLE> entry = m_tip.entry();

        // Remove if missing
        if (! entry)
        {
            erase (view());
            erase (view_cancel());
            continue;
        }

        // Remove if expired
        if (entry->isFieldPresent (sfExpiration) &&
            entry->getFieldU32 (sfExpiration) <= m_when)
        {
            if (j_.trace) j_.trace <<
                "Removing expired offer " << entry->getIndex();
            offerDelete (view_cancel(),
                view_cancel().peek(
                    keylet::offer(entry->key())));
            continue;
        }

        m_offer = Offer (entry, m_tip.quality());

        Amounts const amount (m_offer.amount());

        // Remove if either amount is zero
        if (amount.empty())
        {
            if (j_.warning) j_.warning <<
                "Removing bad offer " << entry->getIndex();
            offerDelete (view_cancel(),
                view_cancel().peek(
                    keylet::offer(entry->key())));
            m_offer = Offer{};
            continue;
        }

        // Calculate owner funds
        // NIKB NOTE The calling code also checks the funds, how expensive is
        //           looking up the funds twice?
        auto const owner_funds = accountFunds(view(),
            m_offer.owner(), amount.out, fhZERO_IF_FROZEN,
                config_);

        // Check for unfunded offer
        if (owner_funds <= zero)
        {
            // If the owner's balance in the pristine view is the same,
            // we haven't modified the balance and therefore the
            // offer is "found unfunded" versus "became unfunded"
            auto const original_funds = accountFunds(view_cancel(),
                m_offer.owner(), amount.out, fhZERO_IF_FROZEN,
                    config_);

            if (original_funds == owner_funds)
            {
                offerDelete (view_cancel(), view_cancel().peek(
                    keylet::offer(entry->key())));
                if (j_.trace) j_.trace <<
                    "Removing unfunded offer " << entry->getIndex();
            }
            else
            {
                if (j_.trace) j_.trace <<
                    "Removing became unfunded offer " << entry->getIndex();
            }
            m_offer = Offer{};
            continue;
        }

        break;
    }

    return true;
}

}
