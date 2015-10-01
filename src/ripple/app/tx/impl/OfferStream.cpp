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
#include <ripple/basics/Log.h>

namespace ripple {

OfferStream::OfferStream (ApplyView& view, ApplyView& cancelView,
    Book const& book, Clock::time_point when,
        StepCounter& counter, beast::Journal journal)
    : j_ (journal)
    , view_ (view)
    , cancelView_ (cancelView)
    , book_ (book)
    , expire_ (when)
    , tip_ (view, book_)
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

    auto p = view.peek (keylet::page(tip_.dir()));

    if (p == nullptr)
    {
        JLOG(j_.error) <<
            "Missing directory " << tip_.dir() <<
            " for offer " << tip_.index();
        return;
    }

    auto v (p->getFieldV256 (sfIndexes));
    auto it (std::find (v.begin(), v.end(), tip_.index()));

    if (it == v.end())
    {
        JLOG(j_.error) <<
            "Missing offer " << tip_.index() <<
            " for directory " << tip_.dir();
        return;
    }

    v.erase (it);
    p->setFieldV256 (sfIndexes, v);
    view.update (p);

    JLOG(j_.trace) <<
        "Missing offer " << tip_.index() <<
        " removed from directory " << tip_.dir();
}

bool
OfferStream::step (Logs& l)
{
    // Modifying the order or logic of these
    // operations causes a protocol breaking change.

    auto viewJ = l.journal ("View");
    for(;;)
    {
        // BookTip::step deletes the current offer from the view before
        // advancing to the next (unless the ledger entry is missing).
        if (! tip_.step(l))
            return false;

        std::shared_ptr<SLE> entry = tip_.entry();

        // If we exceed the maximum number of allowed steps, we're done.
        if (!counter_.step ())
            return false;

        // Remove if missing
        if (! entry)
        {
            erase (view_);
            erase (cancelView_);
            continue;
        }

        // Remove if expired
        if (entry->isFieldPresent (sfExpiration) &&
            entry->getFieldU32 (sfExpiration) <= expire_)
        {
            JLOG(j_.trace) <<
                "Removing expired offer " << entry->getIndex();
            offerDelete (cancelView_,
                cancelView_.peek(keylet::offer(entry->key())), viewJ);
            continue;
        }

        offer_ = Offer (entry, tip_.quality());

        Amounts const amount (offer_.amount());

        // Remove if either amount is zero
        if (amount.empty())
        {
            JLOG(j_.warning) <<
                "Removing bad offer " << entry->getIndex();
            offerDelete (cancelView_,
                cancelView_.peek(keylet::offer(entry->key())), viewJ);
            offer_ = Offer{};
            continue;
        }

        // Calculate owner funds
        auto const owner_funds = accountFunds(view_,
            offer_.owner(), amount.out, fhZERO_IF_FROZEN, viewJ);

        // Check for unfunded offer
        if (owner_funds <= zero)
        {
            // If the owner's balance in the pristine view is the same,
            // we haven't modified the balance and therefore the
            // offer is "found unfunded" versus "became unfunded"
            auto const original_funds = accountFunds(cancelView_,
                offer_.owner(), amount.out, fhZERO_IF_FROZEN, viewJ);

            if (original_funds == owner_funds)
            {
                offerDelete (cancelView_, cancelView_.peek(
                    keylet::offer(entry->key())), viewJ);
                JLOG(j_.trace) <<
                    "Removing unfunded offer " << entry->getIndex();
            }
            else
            {
                JLOG(j_.trace) <<
                    "Removing became unfunded offer " << entry->getIndex();
            }
            offer_ = Offer{};
            continue;
        }

        break;
    }

    return true;
}

}
