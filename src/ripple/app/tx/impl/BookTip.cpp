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

#include <ripple/app/tx/impl/BookTip.h>
#include <ripple/basics/Log.h>

namespace ripple {

BookTip::BookTip(ApplyView& view, Book const& book)
    : view_(view)
    , m_valid(false)
    , m_book(getBookBase(book))
    , m_end(getQualityNext(m_book))
{
}

bool
BookTip::step(beast::Journal j)
{
    if (m_valid)
    {
        if (m_entry)
        {
            offerDelete(view_, m_entry, j);
            m_entry = nullptr;
        }
    }

    for (;;)
    {
        // See if there's an entry at or worse than current quality. Notice
        // that the quality is encoded only in the index of the first page
        // of a directory.
        auto const first_page = view_.succ(m_book, m_end);

        if (!first_page)
            return false;

        unsigned int di = 0;
        std::shared_ptr<SLE> dir;

        if (dirFirst(view_, *first_page, dir, di, m_index))
        {
            m_dir = dir->key();
            m_entry = view_.peek(keylet::offer(m_index));
            m_quality = Quality(getQuality(*first_page));
            m_valid = true;

            // Next query should start before this directory
            m_book = *first_page;

            // The quality immediately before the next quality
            --m_book;

            break;
        }

        // There should never be an empty directory but just in case,
        // we handle that case by advancing to the next directory.
        m_book = *first_page;
    }

    return true;
}

}  // namespace ripple
