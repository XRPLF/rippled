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

#ifndef RIPPLE_CORE_BOOKTIP_H_INCLUDED
#define RIPPLE_CORE_BOOKTIP_H_INCLUDED

#include "Quality.h"
#include "Types.h"

#include "../../beast/beast/utility/noexcept.h"

#include <ostream>
#include <utility>

namespace ripple {
namespace core {

/** Iterates and consumes raw offers in an order book.
    Offers are presented from highest quality to lowest quality. This will
    return all offers present including missing, invalid, unfunded, etc.
*/
class BookTip
{
private:
    std::reference_wrapper <LedgerView> m_view;
    bool m_valid;
    uint256 m_book;
    uint256 m_end;
    uint256 m_dir;
    uint256 m_index;
    SLE::pointer m_entry;

    LedgerView&
    view() const noexcept
    {
        return m_view;
    }

public:
    /** Create the iterator. */
    BookTip (LedgerView& view, BookRef book)
        : m_view (view)
        , m_valid (false)
        , m_book (Ledger::getBookBase (
            book.in.currency, book.in.issuer,
            book.out.currency, book.out.issuer))
        , m_end (Ledger::getQualityNext (m_book))
    {
    }

    uint256 const&
    dir() const noexcept
    {
        return m_dir;
    }

    uint256 const&
    index() const noexcept
    {
        return m_index;
    }

    Quality const
    quality() const noexcept
    {
        return Quality (Ledger::getQuality (m_dir));
    }

    SLE::pointer const&
    entry() const noexcept
    {
        return m_entry;
    }

    /** Erases the current offer and advance to the next offer.
        Complexity: Constant
        @return `true` if there is a next offer
    */
    bool
    step ()
    {
        if (m_valid)
        {
            if (m_entry)
            {
                view().offerDelete (m_index);
                m_entry = nullptr;
            }
        }

        for(;;)
        {
            // See if there's an entry at or worse than current quality.
            auto const page (
                view().getNextLedgerIndex (m_book, m_end));

            if (page.isZero())
                return false;

            unsigned int di (0);
            SLE::pointer dir;
            if (view().dirFirst (page, dir, di, m_index))
            {
                m_dir = dir->getIndex();
                m_entry = view().entryCache (ltOFFER, m_index);
                m_valid = true;

                // Next query should start before this directory
                m_book = page;

                // The quality immediately before the next quality
                --m_book; 

                break;
            }
            // There should never be an empty directory but just in case,
            // we handle that case by advancing to the next directory.
            m_book = page;
        }

        return true;
    }
};

}
}

#endif
