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

#ifndef RIPPLE_CORE_OFFERSTREAM_H_INCLUDED
#define RIPPLE_CORE_OFFERSTREAM_H_INCLUDED

#include "BookTip.h"
#include "Offer.h"
#include "Quality.h"
#include "Types.h"

#include "../../beast/beast/utility/noexcept.h"

#include <ostream>
#include <utility>

namespace ripple {
namespace core {

/** Presents and consumes the offers in an order book.

    Two `LedgerView` objects accumulate changes to the ledger. `view`
    is applied when the calling transaction succeeds. If the calling
    transaction fails, then `view_cancel` is applied.

    Certain invalid offers are automatically removed:
        - Offers with missing ledger entries
        - Offers that expired
        - Offers found unfunded:
            An offer is found unfunded when the corresponding balance is zero
            and the caller has not modified the balance. This is accomplished
            by also looking up the balance in the cancel view.

    When an offer is removed, it is removed from both views. This grooms the
    order book regardless of whether or not the transaction is successful.

    TODO: Remove offers belonging to the taker
*/
class OfferStream
{
protected:
    beast::Journal m_journal;
    std::reference_wrapper <LedgerView> m_view;
    std::reference_wrapper <LedgerView> m_view_cancel;
    Book m_book;
    Clock::time_point m_when;
    BookTip m_tip;
    Offer m_offer;

    // Handle the case where a directory item with no corresponding ledger entry
    // is found. This shouldn't happen but if it does we clean it up.
    void
    erase (LedgerView& view)
    {
        // VFALCO NOTE
        //
        //      This should be using LedgerView::dirDelete, which will
        //      correctly remove the directory if its the last entry.
        //      Unfortunately this is a protocol breaking change.

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

public:
    OfferStream (LedgerView& view, LedgerView& view_cancel, BookRef book,
        Clock::time_point when, beast::Journal journal)
        : m_journal (journal)
        , m_view (view)
        , m_view_cancel (view_cancel)
        , m_book (book)
        , m_when (when)
        , m_tip (view, book)
    {
    }

    LedgerView&
    view() noexcept
    {
        return m_view;
    }

    LedgerView&
    view_cancel() noexcept
    {
        return m_view_cancel;
    }

    Book const&
    book() const noexcept
    {
        return m_book;
    }

uint256 const&
dir() const noexcept
{
    return m_tip.dir();
}

    /** Returns the offer at the tip of the order book.
        Offers are always presented in decreasing quality.
        Only valid if step() returned `true`.
    */
    Offer const&
    tip() const
    {
        return m_offer;
    }

    /** Advance to the next valid offer.
        This automatically removes:
            - Offers with missing ledger entries
            - Offers found unfunded
            - expired offers
        @return `true` if there is a valid offer.
    */
    bool
    step()
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
            // VFALCO NOTE The calling code also checks the funds,
            //             how expensive is looking up the funds twice?
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

#if 0
            // Remove if its our own offer
            //
            // VFALCO NOTE We might not want this for payments
            //
            if (m_account == owner)
            {
                view_cancel().offerDelete (entry->getIndex());
                if (m_journal.trace) m_journal.trace <<
                    "Removing self offer " << entry->getIndex();
                continue;
            }
#endif

            break;
        }

        return true;
    }

    /** Updates the offer to reflect remaining funds.
        The caller is responsible for following all the rounding rules.
        The offer will be considered fully consumed if either the in
        or the out amount is zero.
        @return `true` If the offer had no funds remaining.
    */
    bool
    fill (Amounts const& remaining_funds)
    {
        // Erase the offer if it is fully consumed (in==0 || out==0)
        // This is the same as becoming unfunded
        return false;
    }
};

//------------------------------------------------------------------------------

/**
  Does everything an OfferStream does, and:
  - remove offers that became unfunded (if path is used)
*/
#if 0
class PaymentOfferStream : public OfferStream
{
public:
    PaymentOfferStream (LedgerView& view_base, BookRef book,
        Clock::time_point when, beast::Journal journal)
        : m_journal (journal)
        , m_view (view_base.duplicate())
        , m_view_apply (view_base.duplicate())
        , m_book (book)
        , m_when (when)
    {
    }
};
#endif

//------------------------------------------------------------------------------

/*
TakerOfferStream
  Does everything a PaymentOfferStream does, and:
  - remove offers owned by the taker (if tx succeeds?)
*/

//------------------------------------------------------------------------------

//------------------------------------------------------------------------------

}
}

/*
OfferStream
  - remove offers with missing ledger entries (always)
  - remove expired offers (always)
  - remove offers found unfunded (always)

PaymentOfferStream
  Does everything an OfferStream does, and:
  - remove offers that became unfunded (if path is used)

TakerOfferStream
  Does everything a PaymentOfferStream does, and:
  - remove offers owned by the taker (if tx succeeds?)
*/

#endif

