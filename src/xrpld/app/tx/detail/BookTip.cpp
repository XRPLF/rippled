#include <xrpld/app/tx/detail/BookTip.h>

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
