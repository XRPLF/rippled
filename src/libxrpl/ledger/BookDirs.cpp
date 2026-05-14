/** @file
 *  Implements `BookDirs` and its `const_iterator`, which expose all offers in
 *  one XRPL order-book direction as a flat forward-iterable range.
 *
 *  The underlying ledger stores offers in a two-level, quality-keyed directory
 *  structure.  `BookDirs` hides that structure: the constructor eagerly locates
 *  the first quality directory via `ReadView::succ`, and `operator++` crosses
 *  quality boundaries transparently using `cdirNext` / `succ` / `cdirFirst`.
 *
 *  End-sentinel encoding: both `begin()` and `end()` start from the same
 *  `key_` anchor; the end position is distinguished by `entry_ == 0`,
 *  `cur_key_ == key_`, and `index_ == beast::zero` — the state that
 *  `operator++` restores when iteration is exhausted.
 */
#include <xrpl/ledger/BookDirs.h>

#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Indexes.h>

#include <optional>

namespace xrpl {

/** Locate and prime the first offer in the book.
 *
 *  `root_` is the quality-zero key for this book; `next_quality_` is the
 *  exclusive upper bound of the book's key-space.  `succ` scans the SHAMap
 *  for the smallest key in `(root_, next_quality_)`, which is the first
 *  quality directory that holds at least one offer.  If the book is empty,
 *  `succ` returns nothing and `key_` is `beast::kZERO`, which propagates as
 *  the "empty book" sentinel so that `begin() == end()`.
 *
 *  When a quality directory is found, `cdirFirst` advances `sle_`, `entry_`,
 *  and `index_` to the first offer in that directory.  A well-formed ledger
 *  never has an empty quality directory (offers are removed when consumed),
 *  so the `cdirFirst` failure path is marked `UNREACHABLE` and excluded from
 *  coverage.
 */
BookDirs::BookDirs(ReadView const& view, Book const& book)
    : view_(&view)
    , root_(keylet::page(getBookBase(book)).key)
    , next_quality_(getQualityNext(root_))
    , key_(view_->succ(root_, next_quality_).value_or(beast::kZERO))
{
    XRPL_ASSERT(root_ != beast::kZERO, "xrpl::BookDirs::BookDirs : nonzero root");
    if (key_ != beast::kZERO)
    {
        if (!cdirFirst(*view_, key_, sle_, entry_, index_))
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::BookDirs::BookDirs : directory is empty");
            // LCOV_EXCL_STOP
        }
    }
}

/** Copy the pre-seeded state into the returned iterator.
 *
 *  The constructor has already called `succ` and `cdirFirst`, so this
 *  method simply transfers `next_quality_`, `sle_`, `entry_`, and `index_`
 *  into the new iterator without any additional ledger reads.  When the book
 *  is empty (`key_ == beast::kZERO`) those fields are left at their
 *  zero-initialised defaults, producing an iterator that immediately compares
 *  equal to `end()`.
 */
auto
BookDirs::begin() const -> BookDirs::const_iterator
{
    auto it = BookDirs::const_iterator(*view_, root_, key_);
    if (key_ != beast::kZERO)
    {
        it.next_quality_ = next_quality_;
        it.sle_ = sle_;
        it.entry_ = entry_;
        it.index_ = index_;
    }
    return it;
}

/** Return the end-sentinel iterator.
 *
 *  The private constructor sets `key_ == cur_key_` and leaves `entry_`,
 *  `sle_`, and `index_` at zero — the identical state that `operator++`
 *  restores when the book is exhausted.  Comparing a freshly exhausted
 *  begin iterator against this end iterator therefore yields equality.
 */
auto
BookDirs::end() const -> BookDirs::const_iterator
{
    return BookDirs::const_iterator(*view_, root_, key_);
}

/** Compare two iterators by position within the two-level directory.
 *
 *  The null-pointer early-return guards the common sentinel comparison in
 *  range-for loops: the end iterator constructed by `BookDirs::end()` always
 *  has a valid view, but a default-constructed `const_iterator` does not.
 *  Returning `false` rather than asserting keeps sentinel comparisons safe.
 *
 *  The `XRPL_ASSERT` that follows enforces that the two iterators originate
 *  from the same view and the same book root; cross-book or cross-view
 *  comparisons are programming errors and fire in debug builds.
 *
 *  Position equality requires `entry_`, `cur_key_`, and `index_` to all
 *  agree — together they uniquely identify a slot in the two-level directory.
 */
bool
BookDirs::const_iterator::operator==(BookDirs::const_iterator const& other) const
{
    if (view_ == nullptr || other.view_ == nullptr)
        return false;

    XRPL_ASSERT(
        view_ == other.view_ && root_ == other.root_,
        "xrpl::BookDirs::const_iterator::operator== : views and roots are "
        "matching");
    return entry_ == other.entry_ && cur_key_ == other.cur_key_ && index_ == other.index_;
}

/** Lazily load and cache the current offer SLE.
 *
 *  `index_` is the ledger key of the current offer (set by `cdirFirst` or
 *  `cdirNext`).  The SLE is read from the view on first access and stored in
 *  `cache_` so that repeated dereferences of the same position are cheap.
 *  `operator++` clears `cache_` unconditionally, so callers that advance
 *  without dereferencing pay no read cost for skipped entries.
 */
BookDirs::const_iterator::reference
BookDirs::const_iterator::operator*() const
{
    XRPL_ASSERT(
        index_ != beast::kZERO, "xrpl::BookDirs::const_iterator::operator* : nonzero index");
    if (!cache_)
        cache_ = view_->read(keylet::offer(index_));
    return *cache_;
}

/** Advance to the next offer, crossing quality directories when necessary.
 *
 *  Navigation has three stages:
 *
 *  1. **Within the current quality**: `cdirNext` tries to step to the next
 *     offer in the page chain of `cur_key_`.  On success, `entry_` and
 *     `index_` are updated and we are done.
 *
 *  2. **Cross to the next quality**: if `cdirNext` returns false *and*
 *     `index_` is zero (the page chain truly has no more offers), `succ` is
 *     called with a pre-incremented `cur_key_` to find the next quality
 *     directory strictly after the current one.
 *
 *     @note The `if (index_ == 0)` guard distinguishes "page chain exhausted"
 *     from the rare case where `cdirNext` returned false because the page
 *     contained no `sfIndexes` entries — in that (well-formed-ledger-
 *     impossible) case `index_` is non-zero from the previous step and
 *     the `succ` call is intentionally skipped.
 *
 *  3. **End of book**: if `succ` returns nothing (`cur_key_ == kZERO`), or
 *     if `index_` was non-zero after `cdirNext` failure (see note above),
 *     the iterator is reset to the end-sentinel state (`cur_key_ = key_`,
 *     `entry_ = 0`, `index_ = kZERO`).
 *
 *  When a new quality directory is found, `cdirFirst` positions at its first
 *  offer.  A well-formed ledger never has an empty quality directory, so that
 *  failure path is `UNREACHABLE` and excluded from coverage.
 *
 *  The dereference cache is cleared unconditionally at the end so that
 *  `operator*` on the new position always reads fresh from the view.
 */
BookDirs::const_iterator&
BookDirs::const_iterator::operator++()
{
    using beast::kZERO;

    XRPL_ASSERT(index_ != kZERO, "xrpl::BookDirs::const_iterator::operator++ : nonzero index");
    if (!cdirNext(*view_, cur_key_, sle_, entry_, index_))
    {
        if (index_ == 0)
            cur_key_ = view_->succ(++cur_key_, next_quality_).value_or(kZERO);

        if (index_ != 0 || cur_key_ == kZERO)
        {
            cur_key_ = key_;
            entry_ = 0;
            index_ = kZERO;
        }
        else if (!cdirFirst(*view_, cur_key_, sle_, entry_, index_))
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::BookDirs::const_iterator::operator++ : directory is empty");
            // LCOV_EXCL_STOP
        }
    }

    cache_ = std::nullopt;
    return *this;
}

/** Post-increment: save a copy, advance via `operator++`, return the copy.
 *
 *  The copy preserves `cache_` so the caller can still dereference the
 *  pre-increment position via the returned value; however, this copies the
 *  shared_ptr to the cached SLE rather than re-reading the view.
 */
BookDirs::const_iterator
BookDirs::const_iterator::operator++(int)
{
    XRPL_ASSERT(
        index_ != beast::kZERO, "xrpl::BookDirs::const_iterator::operator++(int) : nonzero index");
    const_iterator tmp(*this);
    ++(*this);
    return tmp;
}

}  // namespace xrpl
