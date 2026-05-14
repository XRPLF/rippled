#pragma once

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Quality.h>

namespace xrpl {

class Logs;

/** Consuming iterator over a DEX order book's quality-sorted offer directories.
 *
 *  Traverses offer directories from best quality (highest exchange rate) to
 *  worst, calling `offerDelete` on each offer before advancing to the next.
 *  Every offer is exposed without filtering — missing SLEs, expired offers,
 *  and unfunded entries are all returned; filtering is the caller's
 *  responsibility (see `TOfferStreamBase`).
 *
 *  Requires `ApplyView` rather than `ReadView` because `step()` physically
 *  removes the current offer from the ledger view on each advance.
 *
 *  @note `BookTip` is a *consuming* iterator: each call to `step()` deletes
 *      the previously yielded offer. Callers that wish to keep an offer must
 *      act on it before calling `step()` again.
 *  @see TOfferStreamBase, OfferStream
 */
class BookTip
{
private:
    ApplyView& view_;
    bool valid_{false};
    uint256 book_;
    uint256 end_;
    uint256 dir_;
    uint256 index_;
    std::shared_ptr<SLE> entry_;
    Quality quality_{};

public:
    /** Construct an iterator positioned before the first offer in a book.
     *
     *  Derives the key-space range for `book` via `getBookBase` (inclusive
     *  lower bound, best quality) and `getQualityNext` (exclusive upper
     *  bound, worst quality). No ledger access occurs until the first call
     *  to `step()`.
     *
     *  @param view The mutable ledger view used for both directory traversal
     *      and offer deletion during `step()`.
     *  @param book The currency/issuer pair identifying the order book to
     *      iterate.
     */
    BookTip(ApplyView& view, Book const& book);

    /** Ledger index of the quality-directory that owns the current offer.
     *
     *  The directory key encodes the exchange rate in the `uint256` index
     *  itself. Valid only after a successful call to `step()`.
     */
    [[nodiscard]] uint256 const&
    dir() const noexcept
    {
        return dir_;
    }

    /** Ledger index of the current offer SLE.
     *
     *  Valid only after a successful call to `step()`.
     */
    [[nodiscard]] uint256 const&
    index() const noexcept
    {
        return index_;
    }

    /** Exchange rate of the current offer's quality directory.
     *
     *  Decoded from the directory's `uint256` key via `getQuality()`.
     *  Valid only after a successful call to `step()`.
     */
    [[nodiscard]] Quality const&
    quality() const noexcept
    {
        return quality_;
    }

    /** Shared pointer to the current offer's SLE, or `nullptr` if missing.
     *
     *  May be `nullptr` when the offer index exists in a directory but
     *  the corresponding ledger entry has been removed. Callers must
     *  null-check before use. Valid only after a successful call to `step()`.
     */
    [[nodiscard]] SLE::pointer const&
    entry() const noexcept
    {
        return entry_;
    }

    /** Delete the current offer from the ledger and advance to the next.
     *
     *  On every invocation after the first, removes the previously yielded
     *  offer via `offerDelete` before searching for the next one. Uses
     *  `view_.succ(book_, end_)` to locate the next occupied quality
     *  directory in O(log n) without scanning empty key ranges.
     *
     *  Empty directories (which should never occur in a well-formed ledger)
     *  are silently skipped rather than treated as fatal errors.
     *
     *  @param j Journal used for diagnostic logging during offer deletion and
     *      directory traversal.
     *  @return `true` if a next offer was found and the accessor fields are
     *      valid; `false` if the book is exhausted.
     *  @note Complexity is O(log n) in the number of ledger entries per call.
     */
    bool
    step(beast::Journal j);
};

}  // namespace xrpl
