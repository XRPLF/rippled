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

namespace ripple {
namespace core {

OfferStream::OfferStream (LedgerView& view, LedgerView& view_cancel,
    BookRef book, Clock::time_point when, beast::Journal journal)
    : m_journal (journal)
    , m_view (view)
    , m_view_cancel (view_cancel)
    , m_book (book)
    , m_when (when)
    , m_tip (view, book)
{
}

// Handle the case where a directory item with no corresponding ledger entry
// is found. This shouldn't happen but if it does we clean it up.
void
OfferStream::erase (LedgerView& view)
{
    // NIKB NOTE This should be using LedgerView::dirDelete, which would
    //           correctly remove the directory if its the last entry.
    //           Unfortunately this is a protocol breaking change.

    auto p (view.entryCache (ltDIR_NODE, m_tip.dir()));

    if (p == nullptr)
    {
        if (m_journal.error) m_journal.error <<
            "Missing directory " << m_tip.dir() <<
            " for offer " << m_tip.index();
        return;
    }

    auto v (p->getFieldV256 (sfIndexes));
    auto& x (v.peekValue());
    auto it (std::find (x.begin(), x.end(), m_tip.index()));

    if (it == x.end())
    {
        if (m_journal.error) m_journal.error <<
            "Missing offer " << m_tip.index() <<
            " for directory " << m_tip.dir();
        return;
    }

    x.erase (it);
    p->setFieldV256 (sfIndexes, v);
    view.entryModify (p);

    if (m_journal.trace) m_journal.trace <<
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

        SLE::pointer const& entry (m_tip.entry());

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
            view_cancel().offerDelete (entry->getIndex());
            if (m_journal.trace) m_journal.trace <<
                "Removing expired offer " << entry->getIndex();
            continue;
        }

        m_offer = Offer (entry, m_tip.quality());

        Amounts const amount (m_offer.amount());

        // Remove if either amount is zero
        if (amount.empty())
        {
            view_cancel().offerDelete (entry->getIndex());
            if (m_journal.warning) m_journal.warning <<
                "Removing bad offer " << entry->getIndex();
            m_offer = Offer{};
            continue;
        }

        // Calculate owner funds
        // NIKB NOTE The calling code also checks the funds, how expensive is
        //           looking up the funds twice?
        Amount const owner_funds (view().accountFunds (
            m_offer.account(), m_offer.amount().out));

        // Check for unfunded offer
        if (owner_funds <= zero)
        {
            // If the owner's balance in the pristine view is the same,
            // we haven't modified the balance and therefore the
            // offer is "found unfunded" versus "became unfunded"
            if (view_cancel().accountFunds (m_offer.account(),
                m_offer.amount().out) == owner_funds)
            {
                view_cancel().offerDelete (entry->getIndex());
                if (m_journal.trace) m_journal.trace <<
                    "Removing unfunded offer " << entry->getIndex();
            }
            else
            {
                if (m_journal.trace) m_journal.trace <<
                    "Removing became unfunded offer " << entry->getIndex();
            }
            m_offer = Offer{};
            continue;
        }

        break;
    }

    return true;
}

bool
OfferStream::step_account (Account const& account)
{
    while (step ())
    {
        if (tip ().account () != account)
            return true;
    }

    return false;
}

}
}
