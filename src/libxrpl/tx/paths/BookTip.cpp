/** @file
 *  Implementation of `BookTip`, the raw destructive cursor over a single
 *  XRPL order book. See `BookTip.h` for the public interface.
 *
 *  Key implementation notes:
 *  - Quality is encoded in the upper 64 bits of each directory's `uint256`
 *    key. Lower numeric values mean better quality (taker pays less).
 *    `book_` is therefore walked upward (numerically) from `getBookBase`
 *    toward `end_` to traverse from best quality to worst.
 *  - `step()` uses a "delete-then-advance" protocol: the offer yielded on
 *    call N is removed from the ledger at the start of call N+1. This lets
 *    `TOfferStreamBase` skip invalid offers simply by calling `step()` again
 *    — no separate deletion call is needed.
 *  - Within a single quality tier, `book_` is set to `*firstPage - 1` after
 *    a successful `dirFirst`. The next `succ` call therefore returns the same
 *    directory again, where `offerDelete` will have removed the just-served
 *    offer and `dirFirst` finds the new head. This re-visitation continues
 *    until the directory is drained, then `succ` advances to the next tier.
 */
#include <xrpl/tx/paths/BookTip.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/OfferHelpers.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <memory>

namespace xrpl {

BookTip::BookTip(ApplyView& view, Book const& book)
    : view_(view), book_(getBookBase(book)), end_(getQualityNext(book_))
{
}

/** Advance the cursor, deleting the previously yielded offer if any.
 *
 *  Delete-then-advance protocol:
 *  - On entry (after the first call), if `valid_` is set and `entry_` is
 *    non-null, the previously yielded offer is permanently removed from the
 *    ledger via `offerDelete`. This means the caller does not need a
 *    separate deletion step — skipping an offer is accomplished simply by
 *    calling `step()` again.
 *
 *  Within-tier re-visitation:
 *  - After a successful `dirFirst`, `book_` is set to `*firstPage - 1`.
 *    On the next call, `succ(book_, end_)` returns the same directory
 *    again. Because the previously yielded offer has by then been deleted,
 *    `dirFirst` finds the new head of that directory. This continues until
 *    the directory is drained, at which point `dirFirst` fails and the
 *    `book_ = *firstPage` fallback causes `succ` to advance to the next
 *    quality tier. No inner loop or separate within-tier cursor is needed.
 *
 *  @param j Journal for diagnostic logging during offer deletion and
 *      directory traversal.
 *  @return `true` if a next offer directory entry was found and `dir_`,
 *      `index_`, `entry_`, and `quality_` are now valid; `false` if the
 *      book is exhausted.
 *  @note Quality is encoded only in the `uint256` key of the *first page*
 *      of each directory; subsequent pages share the same quality.
 *  @note Empty directories (which should never appear in a well-formed
 *      ledger) are silently skipped by advancing `book_` past the empty
 *      page, preventing cursor corruption without crashing.
 */
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
        auto const firstPage = view_.succ(book_, end_);

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

            // Position one step before this directory so the next succ()
            // call re-visits the same tier after the current offer is
            // deleted, draining all offers at this quality before advancing.
            book_ = *firstPage;
            --book_;

            break;
        }

        // Empty directory: advance past it so succ() moves to the next tier.
        book_ = *firstPage;
    }

    return true;
}

}  // namespace xrpl
