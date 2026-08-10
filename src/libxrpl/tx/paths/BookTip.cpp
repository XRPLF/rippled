#include <xrpl/tx/paths/BookTip.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/OfferHelpers.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STLedgerEntry.h>

namespace xrpl {

BookTip::BookTip(ApplyView& view, Book const& book)
    : view_(view), book_(getBookBase(book)), end_(getQualityNext(book_))
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
        auto const firstPage = view_.succ(book_, end_);

        if (!firstPage)
            return false;

        unsigned int di = 0;
        SLE::pointer dir;

        if (dirFirst(view_, *firstPage, dir, di, index_))
        {
            dir_ = dir->key();
            entry_ = view_.peek(keylet::offer(index_));
            quality_ = Quality(getQuality(*firstPage));
            valid_ = true;

            // Next query should start before this directory
            book_ = *firstPage;

            // The quality immediately before the next quality
            --book_;

            break;
        }

        // There should never be an empty directory but just in case,
        // we handle that case by advancing to the next directory.
        book_ = *firstPage;
    }

    return true;
}

}  // namespace xrpl
