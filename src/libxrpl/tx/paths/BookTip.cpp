#include <xrpl/tx/paths/BookTip.h>

namespace xrpl {

BookTip::BookTip(ApplyView& view, Book const& book)
    : view_(view), valid_(false), book_(getBookBase(book)), end_(getQualityNext(book_))
{
}

bool
BookTip::step(beast::Journal j)
{
    if (valid_)
    {
        if (entry_)
        {
            offerDelete(view_, entry_, j);
            entry_ = nullptr;
        }
    }

    for (;;)
    {
        // See if there's an entry at or worse than current quality. Notice
        // that the quality is encoded only in the index of the first page
        // of a directory.
        auto const first_page = view_.succ(book_, end_);

        if (!first_page)
            return false;

        unsigned int di = 0;
        std::shared_ptr<SLE> dir;

        if (dirFirst(view_, *first_page, dir, di, index_))
        {
            dir_ = dir->key();
            entry_ = view_.peek(keylet::offer(index_));
            quality_ = Quality(getQuality(*first_page));
            valid_ = true;

            // Next query should start before this directory
            book_ = *first_page;

            // The quality immediately before the next quality
            --book_;

            break;
        }

        // There should never be an empty directory but just in case,
        // we handle that case by advancing to the next directory.
        book_ = *first_page;
    }

    return true;
}

}  // namespace xrpl
