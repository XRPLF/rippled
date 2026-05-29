#include <xrpl/tx/paths/BookTip.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OrderBookIndex.h>
#include <xrpl/ledger/TopOfBookCache.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/OfferHelpers.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <memory>
#include <optional>

namespace xrpl {

BookTip::BookTip(ApplyView& view, Book const& book)
    : view_(view), originalBook_(book), book_(getBookBase(book)), end_(getQualityNext(book_))
{
}

bool
BookTip::step(beast::Journal j)
{
    bool const firstStep = !valid_;

    if (valid_)
    {
        if (entry_)
        {
            offerDelete(view_, entry_, j);
            entry_ = nullptr;
        }
    }

    // Plan 9: on the first step, ask the order-book index for an ordered
    // snapshot of this book's offers. A returned vector is guaranteed complete
    // (the index rebuilds from authoritative state on a miss), so iterating it
    // is equivalent to the succ() walk — but O(1) per offer instead of an
    // O(log N) trie re-walk. nullopt ⇒ no index ⇒ the succ() path below.
    if (firstStep && OrderBookIndex::enabled())
    {
        if (auto snap = view_.orderedBook(originalBook_))
        {
            cursor_ = std::move(*snap);
            cursorPos_ = 0;
            useCursor_ = true;
        }
    }

    if (useCursor_)
    {
        for (;;)
        {
            if (cursorPos_ >= cursor_.size())
                return false;

            uint256 const offerKey = cursor_[cursorPos_++];
            auto sle = view_.peek(keylet::offer(offerKey));
            if (!sle)
                // The snapshot is from the (immutable) base index; this offer
                // was buffered-deleted by the sandbox (e.g. a pre-crossing
                // cancel). Skip it — the succ() walk wouldn't see it either.
                continue;

            index_ = offerKey;
            dir_ = sle->getFieldH256(sfBookDirectory);
            quality_ = Quality(getQuality(dir_));
            entry_ = std::move(sle);
            valid_ = true;

            // Cursor order must be best-quality-first, exactly like succ().
            XRPL_ASSERT(
                getQuality(dir_) >= lastCursorQuality_,
                "xrpl::BookTip::step : order-book cursor yields non-decreasing quality");
            lastCursorQuality_ = getQuality(dir_);

            return true;
        }
    }

    bool firstIter = firstStep;
    for (;;)
    {
        // See if there's an entry at or worse than current quality. Notice
        // that the quality is encoded only in the index of the first page
        // of a directory.
        std::optional<uint256> firstPage;
        bool fromCache = false;

        if (firstIter && TopOfBookCache::enabled())
        {
            if (auto const cached = view_.topOfBookFirstPage(originalBook_))
            {
                firstPage = *cached;
                fromCache = true;
            }
        }

        if (!firstPage)
        {
            firstPage = view_.succ(book_, end_);
            if (firstIter && firstPage && TopOfBookCache::enabled())
                view_.recordTopOfBook(originalBook_, *firstPage);
        }

#ifndef NDEBUG
        // Differential gate (Plan 8 P8.7): in debug builds every cache hit
        // is shadow-verified against a fresh successor walk. Divergence
        // here is a bug in the invalidation logic, not a fallback.
        if (fromCache && firstPage)
        {
            auto const verified = view_.succ(book_, end_);
            XRPL_ASSERT(
                verified == firstPage,
                "BookTip::step : top-of-book cache hit diverges from succ()");
        }
#endif

        firstIter = false;

        if (!firstPage)
            return false;

        unsigned int di = 0;
        std::shared_ptr<SLE> dir;

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
        // Also covers the case where a stale cache hit returned a
        // page that no longer has any indexes.
        book_ = *firstPage;
    }

    return true;
}

}  // namespace xrpl
