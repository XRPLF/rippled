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

#include <ripple/module/app/book/BookTip.h>

namespace ripple {
namespace core {

BookTip::BookTip (LedgerView& view, BookRef book)
    : m_view (view)
    , m_valid (false)
    , m_book (Ledger::getBookBase (
        book.in.currency, book.in.issuer,
        book.out.currency, book.out.issuer))
    , m_end (Ledger::getQualityNext (m_book))
{
}

bool
BookTip::step ()
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

}
}
